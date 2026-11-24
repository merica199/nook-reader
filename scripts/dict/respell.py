"""
IPA to a plain respelling: /ɪɡˈzæm.pəl/ becomes "ig-ZAM-puhl".

The style is the one newspapers and Wikipedia's respelling key use:
ordinary letters, syllables joined by hyphens, the stressed syllable
in capitals. Built for American (General American) transcriptions,
which is what build_dict.py picks first; British ones come out
readable too.

Steps:
  1. Split the IPA into sounds (longest match first, so "tʃ" is one
     sound, not "t" then "ʃ").
  2. Split the sounds into syllables: one vowel each, Wiktionary's own
     dots and stress marks as fixed breaks, and between them the
     longest consonant cluster that can start an English syllable
     goes to the second one ("ex.am" not "e.xam").
  3. A short vowel ("a" in cat, "i" in sit) keeps the next consonant
     ("HAP-ee", not "HA-pee", which reads as "hah").
  4. Spell each syllable and capitalize the stressed one.

Run this file to check it against a list of known words.
"""
import re
import unicodedata

VOWEL = "V"
CONS = "C"

# Sound table: IPA -> (kind, spelling). Longer keys are tried first.
SOUNDS = {
    # diphthongs and r-colored vowels
    "aɪ": (VOWEL, "eye"), "äɪ": (VOWEL, "eye"), "ɑɪ": (VOWEL, "eye"),
    "aʊ": (VOWEL, "ow"), "äʊ": (VOWEL, "ow"),
    "ɔɪ": (VOWEL, "oy"), "oɪ": (VOWEL, "oy"),
    "oʊ": (VOWEL, "oh"), "əʊ": (VOWEL, "oh"), "ɵʊ": (VOWEL, "oh"),
    "eɪ": (VOWEL, "ay"), "ɛɪ": (VOWEL, "ay"),
    "ɪə": (VOWEL, "eer"), "ɛə": (VOWEL, "air"), "eə": (VOWEL, "air"),
    "ʊə": (VOWEL, "oor"),
    "ɑɹ": (VOWEL, "ar"), "ɑr": (VOWEL, "ar"), "aɹ": (VOWEL, "ar"),
    "ɔɹ": (VOWEL, "or"), "ɔr": (VOWEL, "or"), "oɹ": (VOWEL, "or"),
    "oːɹ": (VOWEL, "or"),
    "ɛɹ": (VOWEL, "air"), "ɛr": (VOWEL, "air"), "eɹ": (VOWEL, "air"),
    "ɪɹ": (VOWEL, "eer"), "ɪr": (VOWEL, "eer"), "iɹ": (VOWEL, "eer"),
    "ʊɹ": (VOWEL, "oor"), "ʊr": (VOWEL, "oor"), "uɹ": (VOWEL, "oor"),
    "ɜɹ": (VOWEL, "ur"), "ɜr": (VOWEL, "ur"), "ʌɹ": (VOWEL, "ur"),
    "əɹ": (VOWEL, "er"), "ər": (VOWEL, "er"),
    "ɝ": (VOWEL, "ur"), "ɚ": (VOWEL, "er"), "ɜ": (VOWEL, "ur"),
    # plain vowels
    "æ": (VOWEL, "a"), "a": (VOWEL, "a"), "ä": (VOWEL, "ah"),
    "ɑ": (VOWEL, "ah"), "ɒ": (VOWEL, "o"), "ɔ": (VOWEL, "aw"),
    "ɛ": (VOWEL, "e"), "e": (VOWEL, "e"), "ɪ": (VOWEL, "i"),
    "ᵻ": (VOWEL, "i"), "ɨ": (VOWEL, "i"), "i": (VOWEL, "ee"),
    "ʊ": (VOWEL, "uu"), "u": (VOWEL, "oo"), "ʉ": (VOWEL, "oo"),
    "ʌ": (VOWEL, "uh"), "ə": (VOWEL, "uh"), "ɐ": (VOWEL, "uh"),
    "ɘ": (VOWEL, "uh"), "o": (VOWEL, "oh"), "ɵ": (VOWEL, "oh"),
    "y": (VOWEL, "ew"), "ø": (VOWEL, "ur"), "œ": (VOWEL, "ur"),
    # consonants
    "t͡ʃ": (CONS, "ch"), "tʃ": (CONS, "ch"),
    "d͡ʒ": (CONS, "j"), "dʒ": (CONS, "j"),
    "ʃ": (CONS, "sh"), "ʒ": (CONS, "zh"), "θ": (CONS, "th"),
    "ð": (CONS, "dh"), "ŋ": (CONS, "ng"), "j": (CONS, "y"),
    "ɹ": (CONS, "r"), "r": (CONS, "r"), "ɻ": (CONS, "r"),
    "ɾ": (CONS, "t"), "ɫ": (CONS, "l"), "ɡ": (CONS, "g"),
    "g": (CONS, "g"), "ʍ": (CONS, "wh"), "x": (CONS, "kh"),
    "ç": (CONS, "h"), "ɬ": (CONS, "hl"), "ʔ": (CONS, ""),
    "p": (CONS, "p"), "b": (CONS, "b"), "t": (CONS, "t"),
    "d": (CONS, "d"), "k": (CONS, "k"), "f": (CONS, "f"),
    "v": (CONS, "v"), "s": (CONS, "s"), "z": (CONS, "z"),
    "h": (CONS, "h"), "m": (CONS, "m"), "n": (CONS, "n"),
    "l": (CONS, "l"), "w": (CONS, "w"),
}
KEYS = sorted(SOUNDS, key=len, reverse=True)

# Sounds a consonant can become when marked syllabic ("button" /n̩/).
SYLLABIC = {"n": "uhn", "l": "uhl", "ɫ": "uhl", "m": "uhm",
            "ɹ": "er", "r": "er"}

# Vowels that sound wrong at the end of a syllable ("ha" reads "hah").
SHORT = {"æ", "a", "ɛ", "e", "ɪ", "ᵻ", "ɒ", "ʌ", "ʊ"}

# Consonant clusters that can start an English syllable.
ONSETS = {
    ("p", "l"), ("p", "ɹ"), ("b", "l"), ("b", "ɹ"), ("t", "ɹ"),
    ("d", "ɹ"), ("k", "l"), ("k", "ɹ"), ("ɡ", "l"), ("ɡ", "ɹ"),
    ("f", "l"), ("f", "ɹ"), ("θ", "ɹ"), ("ʃ", "ɹ"), ("s", "p"),
    ("s", "t"), ("s", "k"), ("s", "m"), ("s", "n"), ("s", "l"),
    ("s", "w"), ("t", "w"), ("d", "w"), ("k", "w"), ("ɡ", "w"),
    ("θ", "w"), ("s", "f"),
    ("s", "p", "l"), ("s", "p", "ɹ"), ("s", "t", "ɹ"),
    ("s", "k", "ɹ"), ("s", "k", "w"), ("s", "k", "l"),
}
for c in ("p", "b", "k", "ɡ", "f", "v", "m", "h"):
    ONSETS.add((c, "j"))
NO_ONSET = {"ŋ"}

# Marks that carry nothing a respelling can show.
IGNORE = set("ːˑ̯̃ʰʷʲ‿͜()/[]-")


def _norm(ipa):
    """One transcription, cleaned: first variant, no optional parts."""
    ipa = unicodedata.normalize("NFC", ipa)
    ipa = re.split(r"[,;~]", ipa)[0]
    ipa = re.sub(r"\([^)]*\)", "", ipa)	# optional sounds: dropped
    ipa = ipa.replace("r", "ɹ").replace("g", "ɡ").replace("'", "ˈ")
    return ipa


def _tokens(ipa):
    """IPA -> list of (kind, ipa) with "ˈ" "ˌ" "." " " as markers."""
    out = []
    i = 0
    while i < len(ipa):
        ch = ipa[i]
        if ch in "ˈˌ. ":
            out.append(("M", ch))
            i += 1
            continue
        if ch == "̩" or ch == "̍":	# syllabic mark
            if out and out[-1][0] == CONS and out[-1][1] in SYLLABIC:
                out[-1] = ("S", out[-1][1])
            i += 1
            continue
        if ch in IGNORE or unicodedata.combining(ch):
            i += 1
            continue
        for k in KEYS:
            if not ipa.startswith(k, i):
                continue
            kind = SOUNDS[k][0]
            # An r-colored vowel only when the r ends the syllable;
            # before a vowel the r starts the next one ("ve-ry").
            if kind == VOWEL and len(k) > 1 and k[-1] in "ɹr":
                rest = ipa[i + len(k):].lstrip("ː")
                nxt = _first_sound(rest)
                if nxt and SOUNDS.get(nxt, (CONS,))[0] == VOWEL:
                    continue
            out.append((kind, k))
            i += len(k)
            break
        else:
            i += 1	# a sign the table doesn't know: skip it
    return out


def _first_sound(s):
    s = s.lstrip(".ˈˌ")
    for k in KEYS:
        if s.startswith(k):
            return k
    return None


def _is_nucleus(tok):
    return tok[0] in (VOWEL, "S")


def _syllables(tokens):
    """Group tokens into syllables: [stress, [tokens], hard_start]."""
    words = [[]]
    for t in tokens:
        if t == ("M", " "):
            words.append([])
        else:
            words[-1].append(t)

    result = []
    for word in words:
        # Pieces between Wiktionary's own marks, each with its stress
        # and whether the break before it is a stress mark.
        pieces = [[0, [], False]]
        for t in word:
            if t[0] == "M":
                if pieces[-1][1]:
                    pieces.append([0, [], False])
                if t[1] == "ˈ":
                    pieces[-1][0], pieces[-1][2] = 1, True
                elif t[1] == "ˌ":
                    pieces[-1][0], pieces[-1][2] = 2, True
            else:
                pieces[-1][1].append(t)

        sylls = []
        for stress, toks, hard in pieces:
            sylls.extend(_split_piece(stress, toks, hard))
        _close_short_vowels(sylls)
        result.append(sylls)
    return result


def _split_piece(stress, toks, hard):
    """Split one piece by its vowels (maximal onset)."""
    nuclei = [i for i, t in enumerate(toks) if _is_nucleus(t)]
    if len(nuclei) <= 1:
        return [[stress, toks, hard]]
    cuts = []
    for a, b in zip(nuclei, nuclei[1:]):
        cluster = [t[1] for t in toks[a + 1:b]]
        # the longest tail of the cluster that can start a syllable
        take = 0
        for n in range(len(cluster), 0, -1):
            tail = tuple(cluster[-n:])
            if (n == 1 and tail[0] not in NO_ONSET) or tail in ONSETS:
                take = n
                break
        cuts.append(b - take)
    out = []
    start = 0
    for i, c in enumerate(cuts):
        out.append([stress if i == 0 else 0, toks[start:c],
                    hard if i == 0 else False])
        start = c
    out.append([0, toks[start:], False])
    return out


def _close_short_vowels(sylls):
    """'ha.pi' -> 'hap.i': a short vowel keeps the next consonant."""
    for cur, nxt in zip(sylls, sylls[1:]):
        if nxt[2]:	# a stress mark: the break is Wiktionary's
            continue
        toks = cur[1]
        if not toks or toks[-1][0] != VOWEL or toks[-1][1] not in SHORT:
            continue
        ntoks = nxt[1]
        if len(ntoks) >= 2 and ntoks[0][0] == CONS and \
                _is_nucleus(ntoks[1]):
            cur[1] = toks + [ntoks[0]]
            nxt[1] = ntoks[1:]


def _spell(stress, toks):
    has_onset = bool(toks) and not _is_nucleus(toks[0])
    has_coda = bool(toks) and not _is_nucleus(toks[-1])
    out = []
    for i, (kind, ipa) in enumerate(toks):
        if kind == "S":
            out.append(SYLLABIC[ipa])
            continue
        sp = SOUNDS[ipa][1]
        if sp == "eye" and has_onset:
            sp = "y"		# "FYT", but "EYE-dee-uh"
        elif ipa == "ʌ" and has_coda:
            sp = "u"		# "BUT", but "BUH-ter"
        elif ipa == "s" and i == len(toks) - 1 and i > 0 and \
                toks[i - 1][0] == VOWEL:
            sp = "ss"		# "EYESS", not "eyes"
        out.append(sp)
    s = "".join(out)
    return s.upper() if stress == 1 else s


def respell(ipa):
    """'/ɪɡˈzæm.pəl/' -> 'ig-ZAM-puhl'. '' if nothing comes out."""
    words = _syllables(_tokens(_norm(ipa)))
    parts = []
    for sylls in words:
        sylls = [s for s in sylls if s[1]]
        # Wiktionary leaves the stress mark off one-syllable words.
        if len(sylls) == 1 and sylls[0][0] == 0:
            sylls[0][0] = 1
        spelled = [_spell(st, toks) for st, toks, _ in sylls]
        spelled = [s for s in spelled if s]
        if spelled:
            parts.append("-".join(spelled))
    return " ".join(parts)


# Known words and what they should come out as.
TESTS = [
    ("/ɪɡˈzæm.pəl/", "ig-ZAM-puhl"),
    ("/ˈdɪk.ʃəˌnɛɹ.i/", "DIK-shuh-ner-ee"),
    ("/pɹəˌnʌn.siˈeɪ.ʃən/", "pruh-nun-see-AY-shuhn"),
    ("/kæt/", "KAT"),
    ("/θɹu/", "THROO"),
    ("/θɔt/", "THAWT"),
    ("/ˈwɔtɚ/", "WAW-ter"),
    ("/ˈbʌtɚ/", "BUT-er"),
    ("/ˈmɛʒɚ/", "MEZH-er"),
    ("/dʒʌdʒ/", "JUJ"),
    ("/tʃɝtʃ/", "CHURCH"),
    ("/sɪŋ/", "SING"),
    ("/əˈbaʊt/", "uh-BOWT"),
    ("/ˈbaɪsɪkəl/", "BY-sik-uhl"),
    ("/ˈneɪtʃɚ/", "NAY-cher"),
    ("/ˈfɑðɚ/", "FAH-dher"),
    ("/hɛɹ/", "HAIR"),
    ("/ˈvɛɹi/", "VER-ee"),
    ("/ˈhæpi/", "HAP-ee"),
    ("/aɪˈdiə/", "eye-DEE-uh"),
    ("/aɪs/", "EYESS"),
    ("/ˈbʌtn̩/", "BUT-uhn"),
    ("/ˈsɪti/", "SIT-ee"),
    ("/ɹɪˈpit/", "ri-PEET"),
    ("/kjut/", "KYOOT"),
    ("/ˈɛv(ə)ɹi/", "EV-ree"),
    ("/ˈfaɪɚ/", "FY-er"),
    ("/bɔɪ/", "BOY"),
    ("/ɡoʊ/", "GOH"),
    ("/ˈwɪndoʊ/", "WIN-doh"),
    ("/ˈjɛlo/", "YEL-oh"),
    ("/kɑɹ/", "KAR"),
    ("/ˌʌn.dɚˈstænd/", "un-der-STAND"),
]

if __name__ == "__main__":
    bad = 0
    for ipa, want in TESTS:
        got = respell(ipa)
        mark = "ok " if got == want else "BAD"
        bad += got != want
        print(f"{mark} {ipa:28} {got:28} {'' if got == want else want}")
    print(f"{len(TESTS) - bad}/{len(TESTS)} right")
