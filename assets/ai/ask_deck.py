#!/usr/bin/env python3
"""Offline voice assistant pipeline for the cyberdeck shell.

  record -> whisper.cpp -> libzim retrieval -> ollama -> piper -> play

Invoked on demand by AskDeckScreen and exits when the interaction ends. Nothing
here is a daemon.

PROTOCOL
    One flat JSON object per line on stdout, every value a string. The C++ side
    reads it with a minimal extractor that skips nested values, so emit() must
    never be handed a dict or list. Diagnostics go to the log file, not stdout;
    any non-JSON line the parent sees is treated as an error diagnostic.

    stages: ready recording transcribing transcript searching context thinking
            token answer speaking done error pull log

    `token` carries incremental reply text as the model generates it. `answer`
    follows with the final, cleaned-up version and is authoritative — the UI
    should replace what it accumulated from tokens rather than append.

    stdin control words: "stop" ends recording, "skip" ends playback.

MEMORY (the whole point of the design — a Pi 4 has 4GB)
    Stages are strictly serialised: whisper exits before ollama starts, and
    ollama unloads before piper starts. Peak RSS is therefore max(stage), not
    the sum. Do not pipeline these, do not pre-warm ollama while whisper runs.
    Ollama is called with keep_alive="0s" so weights are freed immediately
    rather than lingering for its default five minutes.

USAGE
    python3 -u ask_deck.py --voice [--max-seconds 30] [--no-tts] [--no-context]
    python3 -u ask_deck.py --text "how do solar panels work"
    python3 -u ask_deck.py --pull qwen2.5:0.5b
    python3 -u ask_deck.py --selftest

Every setting has an in-script default, so it also runs standalone over SSH.
"""

# Module scope stays stdlib-only and fast. libzim is imported inside
# search_context() so the typed path, --pull and --selftest never pay for it.
import argparse
import html.parser
import json
import os
import re
import select
import shutil
import signal
import subprocess
import sys
import time
import urllib.error
import urllib.request

# --------------------------------------------------------------------------
# configuration
# --------------------------------------------------------------------------

HOME = os.path.expanduser("~")
AI_DIR = os.environ.get("CYBERDECK_AI_DIR", os.path.join(HOME, ".local/share/cyberdeck/ai"))
IS_LINUX = sys.platform.startswith("linux")

TMP_DIR = os.environ.get("CYBERDECK_AI_TMP",
                         "/dev/shm/cyberdeck" if IS_LINUX else "/tmp/cyberdeck")
ZIM_PATH = os.environ.get("CYBERDECK_ZIM", "")
WHISPER_BIN = os.environ.get("CYBERDECK_WHISPER_BIN") or shutil.which("whisper-cli") or ""
WHISPER_MODEL = os.environ.get("CYBERDECK_WHISPER_MODEL",
                               os.path.join(AI_DIR, "models/ggml-tiny.en.bin"))
PIPER_BIN = os.environ.get("CYBERDECK_PIPER_BIN") or shutil.which("piper") or ""
PIPER_VOICE = os.environ.get("CYBERDECK_PIPER_VOICE",
                             os.path.join(AI_DIR, "voices/en_US-amy-low.onnx"))
# The parent resolves this for us, because a GUI-launched app has a minimal PATH
# in which `which ollama` finds nothing.
OLLAMA_BIN = os.environ.get("CYBERDECK_OLLAMA_BIN") or shutil.which("ollama") or ""
OLLAMA_MODEL = os.environ.get("CYBERDECK_OLLAMA_MODEL", "qwen2.5:0.5b")
OLLAMA_URL = os.environ.get("CYBERDECK_OLLAMA_URL", "http://127.0.0.1:11434")
AUDIO_DEVICE = os.environ.get("CYBERDECK_AUDIO_DEVICE", "default")
LOG_PATH = os.environ.get("CYBERDECK_LOG", os.path.join(AI_DIR, "logs/last_run.log"))

CONTEXT_CHARS = 1200      # keep prompts short; Pi CPU inference cost scales with them
RELEVANCE_MIN = 0.34      # lexical-overlap floor for using a retrieved article
NUM_PREDICT = 140      # four short sentences, not an essay
NUM_CTX = 2048

_children = []
_logfile = None


# --------------------------------------------------------------------------
# protocol / plumbing
# --------------------------------------------------------------------------

def emit(**kw):
    """One flat JSON object per line. Values are always strings."""
    sys.stdout.write(json.dumps({k: str(v) for k, v in kw.items()}) + "\n")
    sys.stdout.flush()


def log(msg):
    if _logfile:
        _logfile.write(str(msg).rstrip() + "\n")
        _logfile.flush()


def open_log():
    global _logfile
    try:
        os.makedirs(os.path.dirname(LOG_PATH), exist_ok=True)
        _logfile = open(LOG_PATH, "w")  # truncate: one run's worth, bounded on SD
    except OSError:
        _logfile = None


def spawn(argv, stdin=subprocess.DEVNULL):
    """Start a child with output pinned to the log file.

    Never inherit our stdout: a grandchild holding the write end open would stop
    the parent from ever seeing EOF, and the UI would wait forever.
    """
    sink = _logfile if _logfile else subprocess.DEVNULL
    log("+ " + " ".join(argv))
    proc = subprocess.Popen(argv, stdin=stdin, stdout=sink, stderr=sink)
    _children.append(proc)
    return proc


def kill_children():
    for proc in _children:
        try:
            if proc.poll() is None:
                proc.kill()
        except Exception:
            pass


def on_term(signum, frame):
    # The parent signals our whole process group; tear down promptly so it does
    # not have to escalate to SIGKILL.
    kill_children()
    os._exit(1)


def wait_for_word(word, timeout, proc=None):
    """Wait for a control word on stdin while a child runs.

    Returns True only if the word actually arrived. Closed or absent stdin is
    NOT a reason to stop waiting: run standalone with input redirected, stdin is
    at EOF immediately, and returning then would abandon the child mid-recording
    and truncate the WAV. Once stdin is unusable this just waits out the child.
    """
    deadline = time.monotonic() + timeout
    stdin_usable = True
    while time.monotonic() < deadline:
        if proc is not None and proc.poll() is not None:
            return False  # child finished on its own
        if not stdin_usable:
            time.sleep(0.1)
            continue
        try:
            ready, _, _ = select.select([sys.stdin], [], [], 0.2)
        except (OSError, ValueError):
            stdin_usable = False
            continue
        if ready:
            line = sys.stdin.readline()
            if not line:
                stdin_usable = False  # EOF: keep waiting on the child
                continue
            if line.strip() == word:
                return True
    return False


class Unavailable(Exception):
    """A component is missing or unusable; carries a stable machine code."""

    def __init__(self, code, text=""):
        super().__init__(text or code)
        self.code = code
        self.text = text or code


# --------------------------------------------------------------------------
# stage 1: capture
# --------------------------------------------------------------------------

def record(limit):
    os.makedirs(TMP_DIR, exist_ok=True)
    wav = os.path.join(TMP_DIR, "question.wav")
    if os.path.exists(wav):
        os.remove(wav)

    if IS_LINUX:
        if not shutil.which("arecord"):
            raise Unavailable("no_mic", "arecord not installed (sudo apt install alsa-utils)")
        probe = subprocess.run(["arecord", "-l"], capture_output=True, text=True)
        if probe.returncode != 0 or "no soundcards" in (probe.stdout + probe.stderr).lower():
            raise Unavailable("no_mic", "No capture device found")
        argv = ["arecord", "-q", "-D", AUDIO_DEVICE, "-f", "S16_LE", "-r", "16000",
                "-c", "1", "-d", str(int(limit)), wav]
    else:
        if not shutil.which("sox"):
            raise Unavailable("no_mic", "sox not installed (brew install sox)")
        argv = ["sox", "-d", "-r", "16000", "-c", "1", "-b", "16", wav,
                "trim", "0", str(int(limit))]

    emit(stage="recording", limit=int(limit))
    proc = spawn(argv)

    stopped_early = wait_for_word("stop", limit + 2, proc)
    if stopped_early:
        # SIGINT, not SIGTERM: arecord and sox finalise the WAV header on
        # SIGINT and leave a truncated, unreadable file on SIGTERM.
        proc.send_signal(signal.SIGINT)
    # After SIGINT the recorder only needs a moment to close the file. Otherwise
    # it is still capturing, so allow the rest of the window — a flat few
    # seconds here would kill a 30s recording at the 3 second mark.
    grace = 3 if stopped_early else limit + 5
    try:
        proc.wait(timeout=grace)
    except subprocess.TimeoutExpired:
        proc.send_signal(signal.SIGINT)  # give it a chance to write the header
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()

    if not os.path.exists(wav) or os.path.getsize(wav) < 2048:
        raise Unavailable("no_mic", "Recording produced no audio — check the microphone")
    return wav


# --------------------------------------------------------------------------
# stage 2: transcribe
# --------------------------------------------------------------------------

def transcribe(wav):
    emit(stage="transcribing")
    if not WHISPER_BIN or not os.path.exists(WHISPER_BIN):
        raise Unavailable("no_whisper", "whisper-cli not found — build whisper.cpp")
    if not os.path.exists(WHISPER_MODEL):
        raise Unavailable("no_whisper_model", "whisper model missing — run AI ASSETS setup")

    base = os.path.join(TMP_DIR, "transcript")
    out = base + ".txt"
    if os.path.exists(out):
        os.remove(out)

    # -t 4 uses all four Pi cores; -bs 1 -bo 1 is greedy decoding, materially
    # faster with negligible quality loss on a short spoken question.
    proc = spawn([WHISPER_BIN, "-m", WHISPER_MODEL, "-f", wav, "-nt", "-np",
                  "-otxt", "-of", base, "-t", "4", "-bs", "1", "-bo", "1"])
    proc.wait()
    if proc.returncode != 0 or not os.path.exists(out):
        raise Unavailable("whisper_failed", "Transcription failed — see the log")

    with open(out, "r", errors="replace") as fh:
        text = fh.read()
    os.remove(out)
    try:
        os.remove(wav)
    except OSError:
        pass

    text = re.sub(r"\[[^\]]*\]", " ", text)   # [BLANK_AUDIO] and friends
    text = re.sub(r"\s+", " ", text).strip()
    if len(text) < 2:
        raise Unavailable("no_speech", "Didn't catch that.")
    return text


# --------------------------------------------------------------------------
# stage 3: retrieval
# --------------------------------------------------------------------------
class _Article(html.parser.HTMLParser):
    """Whole-article plain text with block boundaries kept.

    The previous version stopped at the first <h2> and returned only the lead.
    That is fine for "what is X" and useless for "how does X work", where the
    answer lives in a section further down — so the entire article is extracted
    and the best passages are chosen afterwards.
    """

    BLOCK = {"p", "li", "h2", "h3", "h4", "div", "br", "tr", "blockquote"}
    DROP = {"script", "style", "sup", "table", "figure", "figcaption", "audio",
            "video", "nav", "footer", "header"}

    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.parts = []
        self.skip = 0

    def handle_starttag(self, tag, attrs):
        if tag in self.DROP:
            self.skip += 1
        elif tag in self.BLOCK:
            self.parts.append("\n\n")

    def handle_endtag(self, tag):
        if tag in self.DROP and self.skip > 0:
            self.skip -= 1
        elif tag in self.BLOCK:
            self.parts.append("\n\n")

    def handle_data(self, data):
        if self.skip == 0:
            self.parts.append(data)

    def text(self):
        raw = "".join(self.parts)
        raw = re.sub(r"[ \t\r\f\v]+", " ", raw)
        raw = re.sub(r"\n\s*\n\s*(\n\s*)+", "\n\n", raw)
        return raw.strip()


STOPWORDS = {
    "the", "a", "an", "of", "is", "are", "was", "were", "be", "been", "and", "or",
    "but", "if", "then", "than", "that", "this", "these", "those", "what", "which",
    "who", "whom", "how", "why", "when", "where", "does", "do", "did", "can",
    "could", "should", "would", "will", "shall", "to", "in", "on", "for", "with",
    "from", "about", "into", "over", "after", "before", "between", "it", "its",
    "as", "at", "by", "not", "no", "yes", "you", "your", "me", "my", "we", "our",
    "they", "their", "he", "she", "his", "her", "some", "any", "all", "there",
    "here", "tell", "explain", "describe", "please", "much", "many", "most",
}

# Dropped from the *query* only, never from passage scoring. In "how do solar
# panels work" the verb is part of the question, not the topic — leaving it in
# made "Solar car" outrank "Solar cell" because that article happened to contain
# both "panels" and "work".
QUESTION_VERBS = {
    "work", "works", "working", "happen", "happens", "mean", "means", "called",
    "made", "make", "makes", "use", "used", "uses", "using", "get", "gets",
    "become", "becomes", "look", "looks", "like",
}

# Passages that are licence footers or cross-wiki pointers rather than content.
BOILERPLATE = re.compile(
    r"(?i)(issued from wikipedia|creative commons attribution|additional terms may "
    r"apply|simple english wiktionary has a definition|this page was last|retrieved "
    r"from|may be available under|list of articles)"
)

MAX_ARTICLES = 6        # index hits to open
MAX_PASSAGES = 4        # passages injected into the prompt
MAX_PER_ARTICLE = 2     # keeps one article from crowding out the rest
CHUNK_TARGET = 420      # characters per passage, roughly a paragraph
PASSAGE_SCORE_RATIO = 0.55   # a passage must score this fraction of the best


def tokens(text):
    return {w for w in re.findall(r"[a-z0-9]+", text.lower())
            if len(w) > 2 and w not in STOPWORDS}


def query_terms(question):
    """Content words, in order, deduplicated.

    Used both for the index query and for scoring. The interrogative opener is
    dropped because it actively hurts ranking: "what is photosynthesis" ranks
    Ecosystem and Leaf above the Photosynthesis article, which does not even
    make the top five, while "photosynthesis" alone puts it first.
    """
    seen, out = set(), []
    for w in re.findall(r"[A-Za-z0-9']+", question):
        lw = w.lower()
        if len(lw) > 1 and lw not in STOPWORDS and lw not in QUESTION_VERBS \
                and lw not in seen:
            seen.add(lw)
            out.append(lw)
    if not out:  # a question made entirely of stopwords; fall back to content words
        out = [w.lower() for w in re.findall(r"[A-Za-z0-9']+", question)
               if w.lower() not in STOPWORDS][:4]
    return out


def strip_repeated_title(title, body):
    """ZIM articles open with the title repeated as heading markup.

    Left alone the passage begins "Photosynthesis Photosynthesis Photosynthesis
    is a process..." which burns prompt budget and reads like a bug.
    """
    t = title.strip()
    if not t:
        return body
    out = body.lstrip()
    lowered_t = t.lower()
    while out[: len(t)].lower() == lowered_t:
        tail = out[len(t):]
        # Only strip on a word boundary. Without this the title
        # "Heterojunction solar cell" eats into "Heterojunction solar cells
        # (HJT)…" and leaves the passage starting mid-word at "s (HJT)".
        if tail[:1].isalnum():
            break
        rest = tail.lstrip()
        if not rest:
            break
        # Keep the copy that starts the real sentence ("Photosynthesis is a …").
        if re.match(r"(?i)^(is|are|was|were|has|have|refers|means)\b", rest):
            break
        out = rest
    return out


def chunk_article(title, body):
    """Split an article into passage-sized pieces on paragraph boundaries."""
    body = strip_repeated_title(title, body)
    paras = [p.strip() for p in body.split("\n\n")
             if len(p.strip()) > 40 and not BOILERPLATE.search(p)]
    chunks, buf = [], ""
    for para in paras:
        if not buf:
            buf = para
        elif len(buf) + 1 + len(para) <= CHUNK_TARGET:
            buf += " " + para
        else:
            chunks.append(buf)
            buf = para
        while len(buf) > CHUNK_TARGET * 2:   # one enormous paragraph
            cut = buf.rfind(" ", 0, CHUNK_TARGET)
            if cut < 80:
                cut = CHUNK_TARGET
            chunks.append(buf[:cut].strip())
            buf = buf[cut:].strip()
    if buf:
        chunks.append(buf)
    return chunks


def title_bonus(question, title):
    """Reward an article whose subject *is* the question's subject.

    Term overlap alone cannot separate "Photosynthesis" from "Leaf" for "what is
    photosynthesis" — the word appears in both. Matching the title against the
    question breaks the tie toward the article actually about it.
    """
    t = title.strip().lower()
    q = question.strip().lower()
    if not t:
        return 0.0
    if t == q:
        return 1.0
    qt = set(query_terms(question))
    tt = set(query_terms(title))
    if not tt:
        return 0.0
    if tt == qt:
        return 0.9
    if tt <= qt:                 # "solar cell" for "solar panels work"
        return 0.6
    if t in q:
        return 0.5
    return 0.0


def score_passage(qterms, weights, passage):
    """Weighted coverage of the question's terms by this passage.

    Rare terms count for more, so a passage that merely repeats a common word
    ("solar") cannot outrank one that actually contains the specific term. This
    is what stopped the Heterojunction article beating Solar cell purely because
    it said "panels" twice.
    """
    if not qterms:
        return 0.0
    present = tokens(passage)
    hit = sum(weights[t] for t in qterms if t in present)
    total = sum(weights[t] for t in qterms) or 1.0
    coverage = hit / total
    # Mild preference for meatier passages; very short ones rarely answer much.
    length_factor = min(1.0, len(passage) / 200.0)
    return coverage * (0.7 + 0.3 * length_factor)


def relevance(question, title, snippet):
    """Kept for the gate: unweighted fraction of question words present."""
    q = set(query_terms(question))
    if not q:
        return 0.0
    t = tokens(title + " " + snippet)
    return len(q & t) / len(q)


def search_context(question):
    """Passage-level retrieval over the local ZIM.

    Opens the top index hits, splits them into passages, scores every passage
    against the question and injects the best few — rather than the opening
    paragraph of a single article, which frequently does not contain the answer.
    Returns (sources, passages) or (None, None).
    """
    emit(stage="searching")
    if not ZIM_PATH or not os.path.exists(ZIM_PATH):
        emit(stage="context", used="0", reason="no_zim")
        return None, None

    try:
        from libzim.reader import Archive
        from libzim.search import Query, Searcher
    except ImportError:
        emit(stage="context", used="0", reason="no_zim")
        return None, None

    try:
        try:
            archive = Archive(ZIM_PATH)
        except TypeError:  # some libzim builds insist on a pathlib.Path
            import pathlib
            archive = Archive(pathlib.Path(ZIM_PATH))

        searcher = Searcher(archive)
        terms = query_terms(question)
        index_query = " ".join(terms) if terms else question

        paths = list(searcher.search(Query().set_query(index_query))
                     .getResults(0, MAX_ARTICLES))
        if not paths and index_query != question:
            paths = list(searcher.search(Query().set_query(question))
                         .getResults(0, MAX_ARTICLES))
        if not paths:
            emit(stage="context", used="0", reason="no_hit")
            return None, None

        # Collect candidate passages from every hit.
        candidates = []   # (title, rank, chunk_index, chunk)
        for rank, path in enumerate(paths):
            try:
                entry = archive.get_entry_by_path(path)
                raw = bytes(entry.get_item().content).decode("utf-8", "ignore")
            except Exception:
                continue
            parser = _Article()
            parser.feed(raw)
            body = parser.text()
            if not body:
                continue
            for i, chunk in enumerate(chunk_article(entry.title, body)):
                candidates.append((entry.title, rank, i, chunk))
        if not candidates:
            emit(stage="context", used="0", reason="no_hit")
            return None, None

        # Term weights: a term appearing in nearly every candidate tells us
        # little, so weight by inverse document frequency over the candidate set.
        import math
        n = len(candidates)
        weights = {}
        for t in terms:
            df = sum(1 for c in candidates if t in tokens(c[3]))
            weights[t] = math.log(1.0 + n / (1.0 + df))
        if not any(weights.values()):
            weights = {t: 1.0 for t in terms}

        scored = []
        for title, rank, idx, chunk in candidates:
            s = score_passage(terms, weights, chunk)
            s += title_bonus(question, title) * 0.35    # right article
            s += max(0.0, 0.12 - 0.02 * rank)          # trust the index a little
            if idx == 0:
                s += 0.10                              # leads are definitional
            scored.append((s, title, idx, chunk))
        scored.sort(key=lambda x: -x[0])

        best_overlap = relevance(question, scored[0][1], scored[0][3])
        if best_overlap < RELEVANCE_MIN:
            emit(stage="context", used="0", reason="low_score",
                 score="%.2f" % best_overlap, title=scored[0][1])
            return None, None

        # Only keep passages that are in the same league as the best one.
        # Filling the remaining budget with whatever ranked next drags in
        # loosely-related articles — "Chlorella" for photosynthesis, "Power
        # Purchasing Agreement" for solar panels — which give a small model more
        # opportunity to wander than they give it facts.
        cutoff = scored[0][0] * PASSAGE_SCORE_RATIO

        chosen, per_article, budget = [], {}, CONTEXT_CHARS
        for s, title, idx, chunk in scored:
            if len(chosen) >= MAX_PASSAGES or budget <= 120:
                break
            if chosen and s < cutoff:
                break
            if per_article.get(title, 0) >= MAX_PER_ARTICLE:
                continue
            text = chunk[:budget]
            chosen.append((title, text))
            per_article[title] = per_article.get(title, 0) + 1
            budget -= len(text)

        sources = []
        for title, _ in chosen:
            if title not in sources:
                sources.append(title)

        emit(stage="context", used="1", title=sources[0],
             sources="; ".join(sources), passages=str(len(chosen)),
             score="%.2f" % best_overlap)
        return sources, chosen

    except Exception as exc:  # noqa: BLE001 - retrieval is best-effort
        log("zim error: %r" % (exc,))
        emit(stage="context", used="0", reason="zim_error")
        return None, None



SYSTEM = (
    "You are DECK, the offline assistant inside a Raspberry Pi cyberdeck.\n"
    "Answer in at most 4 short sentences. Plain text only — no markdown, no "
    "lists, no headings. If you do not know, say so plainly. Never invent "
    "citations or URLs.\n"
)

CONTEXT_RULES = (
    "Below are passages from an offline copy of Wikipedia. Base your answer on "
    "them wherever they are relevant, and prefer their wording and facts over "
    "your own recollection. If they do not cover the question, ignore them "
    "completely and answer from your own knowledge — and say plainly that you "
    "are unsure if you are. Never cite a source that is not listed below, and "
    "do not mention these instructions.\n"
)


def build_prompt(question, sources, passages):
    """passages is a list of (article_title, text)."""
    parts = [SYSTEM]
    if passages:
        parts.append("\n" + CONTEXT_RULES)
        parts.append("\n--- REFERENCE PASSAGES (offline Wikipedia) ---\n")
        for i, (title, text) in enumerate(passages, 1):
            parts.append('[%d] from "%s": %s\n' % (i, title, text))
        parts.append("--- END REFERENCE PASSAGES ---\n")
    parts.append("\nQUESTION: %s\nANSWER:" % question)
    return "".join(parts)


MAX_SENTENCES = 4


def tidy_answer(text):
    """Force model output into something worth reading aloud.

    A 0.5b model ignores "plain text, four sentences" a good fraction of the
    time — the prompt asks, this enforces. Piper would happily pronounce
    "\\[ 6CO_2 + 6H_2O \\rightarrow ... \\]" one character at a time, so LaTeX,
    markdown and runaway length are stripped rather than trusted away.
    """
    t = text.strip()

    # Display maths, in the several forms small models emit.
    t = re.sub(r"\\\[.*?\\\]", " ", t, flags=re.S)
    t = re.sub(r"\\\(.*?\\\)", " ", t, flags=re.S)
    t = re.sub(r"\$\$.*?\$\$", " ", t, flags=re.S)
    t = re.sub(r"\$[^$\n]{1,80}\$", " ", t)
    t = re.sub(r"\\[a-zA-Z]+\s*", " ", t)      # stray \rightarrow, \times …

    # Fenced and inline code.
    t = re.sub(r"```.*?```", " ", t, flags=re.S)
    t = t.replace("`", "")

    # Markdown emphasis, headings and list bullets.
    t = re.sub(r"\*\*(.*?)\*\*", r"\1", t, flags=re.S)
    t = re.sub(r"(?m)^\s{0,3}#{1,6}\s*", "", t)
    t = re.sub(r"(?m)^\s*[-*+]\s+", "", t)
    t = re.sub(r"(?m)^\s*\d+[.)]\s+", "", t)
    t = t.replace("*", "").replace("_", " ")

    # Symbols a TTS voice either skips or spells out one glyph at a time.
    for sym, word in (("→", " yields "), ("->", " yields "), ("≈", " about "),
                      ("±", " plus or minus "), ("×", " times "), ("≤", " at most "),
                      ("≥", " at least ")):
        t = t.replace(sym, word)

    t = re.sub(r"\s+", " ", t).strip()
    if not t:
        return ""

    # Keep the opening sentences; a small model's later ones tend to restate.
    parts = [p.strip() for p in re.split(r"(?<=[.!?])\s+", t) if p.strip()]

    # Removing a formula leaves its legend behind as gibberish — "Where: is the
    # radial distance from the center." Those fragments read badly and sound
    # worse, so drop sentences that no longer have a subject.
    def is_fragment(s):
        if re.match(r"(?i)^where\b\s*:?\s*$", s):
            return True
        if re.match(r"(?i)^where\s*:\s*(is|are|the)\b", s):
            return True
        if re.match(r"(?i)^(is|are|was|were|and|or|then)\b", s):
            return True
        if re.match(r"^[:;,\-–—]", s):
            return True
        # "... using the following formula:" with the formula now gone.
        if re.search(r"(?i)\b(following|below)\s+(formula|equation)\s*:?\s*$", s):
            return True
        return False

    kept = [p for p in parts if not is_fragment(p)][:MAX_SENTENCES]
    out = " ".join(kept).strip()

    # Never end mid-thought if the model was cut off by num_predict.
    if out and out[-1] not in ".!?":
        cut = max(out.rfind("."), out.rfind("!"), out.rfind("?"))
        if cut > 40:
            out = out[: cut + 1]
    return out


def probe_ollama(url, timeout=1.0):
    try:
        with urllib.request.urlopen(url + "/api/tags", timeout=timeout):
            return True
    except Exception:
        return False


def ensure_ollama(url):
    """Return the server process if we started it, else None.

    A systemd-managed ollama is left alone — we only tear down what we own.
    Either way keep_alive="0s" frees the model weights after each request.
    """
    if probe_ollama(url):
        log("ollama already running (not ours)")
        return None
    if not OLLAMA_BIN:
        raise Unavailable("no_ollama", "Ollama is not installed")

    # Deliberately no start_new_session: the server must stay inside our
    # process group so the parent's group signal reaches it on cancel.
    proc = spawn([OLLAMA_BIN, "serve"])
    for _ in range(80):  # up to 20s
        if probe_ollama(url):
            return proc
        if proc.poll() is not None:
            raise Unavailable("ollama_start_failed", "ollama serve exited immediately")
        time.sleep(0.25)
    proc.kill()
    raise Unavailable("ollama_start_timeout", "Ollama did not start in time")


def generate(question, sources, passages):
    emit(stage="thinking")
    owned = ensure_ollama(OLLAMA_URL)
    try:
        payload = {
            "model": OLLAMA_MODEL,
            "prompt": build_prompt(question, sources, passages),
            # Streamed so the UI can show words appearing instead of a spinner
            # followed by a wall of text. The full reply is still tidied at the
            # end and re-sent as the authoritative `answer`.
            "stream": True,
            # Frees the weights the moment the response is done instead of
            # holding ~400MB-1.3GB for ollama's default five minutes.
            "keep_alive": "0s",
            "options": {
                "num_predict": NUM_PREDICT,
                "temperature": 0.4,
                "num_ctx": NUM_CTX,
            },
        }
        request = urllib.request.Request(
            OLLAMA_URL + "/api/generate",
            data=json.dumps(payload).encode(),
            headers={"Content-Type": "application/json"},
        )
        raw_parts = []
        try:
            with urllib.request.urlopen(request, timeout=300) as resp:
                for chunk in resp:
                    chunk = chunk.strip()
                    if not chunk:
                        continue
                    try:
                        obj = json.loads(chunk.decode("utf-8", "ignore"))
                    except ValueError:
                        continue
                    if obj.get("error"):
                        raise Unavailable("ollama_error", str(obj["error"]))
                    piece = obj.get("response") or ""
                    if piece:
                        raw_parts.append(piece)
                        # Light touch only: the full clean-up needs the whole
                        # reply, so just keep obvious markup out of the live view.
                        emit(stage="token", text=piece.replace("*", "").replace("`", ""))
                    if obj.get("done"):
                        break
        except urllib.error.HTTPError as exc:
            detail = exc.read().decode("utf-8", "ignore")[:200]
            if "not found" in detail.lower():
                raise Unavailable("no_ollama_model",
                                  "Model %s is not installed" % OLLAMA_MODEL)
            raise Unavailable("ollama_http_error", detail)
        except urllib.error.URLError as exc:
            raise Unavailable("ollama_unreachable", str(exc.reason))

        raw = "".join(raw_parts).strip()
        if not raw:
            raise Unavailable("empty_answer", "The model returned nothing")
        answer = tidy_answer(raw)
        if not answer:
            # Everything got stripped (pure LaTeX, say). Fall back to the raw
            # text rather than reporting nothing at all.
            answer = re.sub(r"\s+", " ", raw)[:400]
        if answer != raw:
            log("tidied answer:\n  raw: %s\n  out: %s" % (raw[:400], answer))
        return answer
    finally:
        if owned is not None:
            owned.terminate()
            try:
                owned.wait(timeout=3)
            except subprocess.TimeoutExpired:
                owned.kill()


# --------------------------------------------------------------------------
# stage 5: speak
# --------------------------------------------------------------------------

def speak(text):
    if not PIPER_BIN or not os.path.exists(PIPER_BIN):
        log("piper missing; skipping TTS")
        return
    if not os.path.exists(PIPER_VOICE):
        log("piper voice missing; skipping TTS")
        return

    os.makedirs(TMP_DIR, exist_ok=True)
    wav = os.path.join(TMP_DIR, "answer.wav")
    emit(stage="speaking")

    synth = subprocess.Popen(
        [PIPER_BIN, "-m", PIPER_VOICE, "-f", wav,
         "--length_scale", "1.0", "--sentence_silence", "0.2"],
        stdin=subprocess.PIPE,
        stdout=_logfile or subprocess.DEVNULL,
        stderr=_logfile or subprocess.DEVNULL,
    )
    _children.append(synth)
    try:
        synth.communicate(text.encode("utf-8"), timeout=120)
    except subprocess.TimeoutExpired:
        synth.kill()
        return
    if not os.path.exists(wav):
        log("piper produced no wav")
        return

    play_argv = ["aplay", "-q", wav] if IS_LINUX else ["afplay", wav]
    if not shutil.which(play_argv[0]):
        log("no player available (%s)" % play_argv[0])
        return
    player = spawn(play_argv)
    # Interruptible: the UI sends "skip" when the user does not want to wait.
    if wait_for_word("skip", 600, player):
        player.terminate()
    try:
        player.wait(timeout=3)
    except subprocess.TimeoutExpired:
        player.kill()
    try:
        os.remove(wav)
    except OSError:
        pass


# --------------------------------------------------------------------------
# modes
# --------------------------------------------------------------------------

def run_query(question, do_tts, use_context):
    emit(stage="transcript", text=question)
    sources, passages = (None, None)
    if use_context:
        sources, passages = search_context(question)
    answer = generate(question, sources, passages)
    emit(stage="answer", text=answer)
    if do_tts:
        speak(answer)
    emit(stage="done")
    return 0


def run_pull(tag):
    """Stream /api/pull so the downloader gets real byte progress.

    `ollama pull` on the CLI repaints with ANSI escapes and cannot be parsed.
    """
    owned = ensure_ollama(OLLAMA_URL)
    try:
        request = urllib.request.Request(
            OLLAMA_URL + "/api/pull",
            data=json.dumps({"name": tag, "stream": True}).encode(),
            headers={"Content-Type": "application/json"},
        )
        last = -1
        with urllib.request.urlopen(request, timeout=3600) as resp:
            for raw in resp:
                raw = raw.strip()
                if not raw:
                    continue
                try:
                    obj = json.loads(raw.decode("utf-8", "ignore"))
                except ValueError:
                    continue
                if obj.get("error"):
                    emit(stage="error", code="pull_failed", text=str(obj["error"]))
                    return 1
                total = obj.get("total") or 0
                completed = obj.get("completed") or 0
                if total > 0:
                    pct = int(completed * 100 / total)
                    if pct != last:
                        last = pct
                        emit(stage="pull", pct=pct)
                elif obj.get("status"):
                    log("pull: %s" % obj["status"])
        emit(stage="pull", pct=100)
        emit(stage="done")
        return 0
    except urllib.error.URLError as exc:
        emit(stage="error", code="ollama_unreachable", text=str(exc.reason))
        return 1
    finally:
        if owned is not None:
            owned.terminate()
            try:
                owned.wait(timeout=3)
            except subprocess.TimeoutExpired:
                owned.kill()


def run_selftest():
    """One implementation of the readiness checklist, two consumers:
    AiAssets::startVerify() and a human over SSH."""
    ok = True

    def check(key, condition, detail):
        nonlocal ok
        if not condition:
            ok = False
        emit(stage="log", key=key, ok="1" if condition else "0", text=detail)

    check("tmp", os.path.isdir(TMP_DIR) or not os.path.exists(TMP_DIR),
          "temp dir %s" % TMP_DIR)

    if IS_LINUX:
        check("arecord", bool(shutil.which("arecord")), "arecord (alsa-utils)")
        check("aplay", bool(shutil.which("aplay")), "aplay (alsa-utils)")
    else:
        check("sox", bool(shutil.which("sox")), "sox (brew install sox)")
        check("afplay", bool(shutil.which("afplay")), "afplay")

    check("whisper-bin", bool(WHISPER_BIN) and os.path.exists(WHISPER_BIN),
          WHISPER_BIN or "whisper-cli not found")
    check("whisper-model", os.path.exists(WHISPER_MODEL), WHISPER_MODEL)
    check("piper-bin", bool(PIPER_BIN) and os.path.exists(PIPER_BIN),
          PIPER_BIN or "piper not found")
    check("piper-voice", os.path.exists(PIPER_VOICE), PIPER_VOICE)
    check("ollama-bin", bool(OLLAMA_BIN), OLLAMA_BIN or "ollama not found")

    try:
        import libzim  # noqa: F401
        check("libzim", True, "libzim importable")
    except ImportError:
        check("libzim", False, "pip install libzim")

    # The ZIM is optional: without it answers are simply ungrounded.
    emit(stage="log", key="zim", ok="1" if (ZIM_PATH and os.path.exists(ZIM_PATH)) else "0",
         text=ZIM_PATH or "no ZIM configured (optional)")

    emit(stage="done" if ok else "error", code="" if ok else "selftest_failed")
    return 0 if ok else 1


def main():
    signal.signal(signal.SIGTERM, on_term)
    signal.signal(signal.SIGINT, on_term)
    open_log()

    ap = argparse.ArgumentParser()
    ap.add_argument("--voice", action="store_true")
    ap.add_argument("--text", default=None)
    ap.add_argument("--pull", default=None)
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--max-seconds", type=float, default=30.0)
    ap.add_argument("--no-tts", action="store_true")
    ap.add_argument("--no-context", action="store_true")
    args = ap.parse_args()

    emit(stage="ready")

    try:
        if args.selftest:
            return run_selftest()
        if args.pull:
            return run_pull(args.pull)
        if args.voice:
            wav = record(args.max_seconds)
            question = transcribe(wav)
        elif args.text:
            question = args.text.strip()
            if not question:
                raise Unavailable("no_speech", "Empty question")
        else:
            emit(stage="error", code="bad_args", text="need --voice, --text, --pull or --selftest")
            return 2
        return run_query(question, not args.no_tts, not args.no_context)

    except Unavailable as exc:
        log("unavailable: %s: %s" % (exc.code, exc.text))
        emit(stage="error", code=exc.code, text=exc.text)
        return 1
    except Exception as exc:  # noqa: BLE001 - the UI must always get a reason
        import traceback
        log(traceback.format_exc())
        emit(stage="error", code="helper_exception", text=repr(exc))
        return 1
    finally:
        kill_children()


if __name__ == "__main__":
    sys.exit(main())
