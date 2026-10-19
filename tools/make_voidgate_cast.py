"""Void Gate's own cast animation (user: "a cast with the hand and sounds of its own"): Mesmerize's hand (aim at a
spot, release: what the gates do too) instead of Far Reach's whip and tendril.

The power is a Far Reach (cpntPowerGrapple): it plays m_inMidnight (hand up), m_loopMidnight (aiming) and
m_outMidnight (release), and casts on the release scene's ARK_MDNT_MSG_GRAPPLE_CAST_BEAM event -- the module's cast
hook puts the gate there. Mesmerize's scenes (left arm only, its own sounds: cast hum, "open") have no such event:
our copy of the release scene gets one, at the thrust of the hand.

  py tools\\make_voidgate_cast.py        writes the 3 scenes and points the power decl at them
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from d2mod import Stock, CATALOGS  # noqa: E402

MOD = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "d2mods", "VoidGate")
SRC = "generated/decls/midnightscene/midnight/gameplay/player/base/powers/power_mesmerize_cast_{}.midnightscene.decl"
DST = "generated/decls/midnightscene/midnight/gameplay/player/base/powers/voidgate_cast_{}.midnightscene.decl"
SCENE = "midnight/gameplay/player/base/powers/voidgate_cast_{}.midnightscene"
POWER = "generated/decls/cpntpowergrapple/components/characters/player/base/powers/voidgate/voidgate.cpntpowergrapple.decl"
CAST_AT = 200  # release scene time of the cast event (the clip runs 0..666): the hand thrusts forward

EVENT_TRACK = """									item[{n}] = {{
										m_class = "arkMidnightTrack";
										m_object = {{
											m_keys = {{
												num = 1;
												item[0] = {{
													m_handle = {{
														sceneUID = 1;
														groupUID = 1;
														trackType = "EMTT_EVENT";
														keyUID = 1;
													}}
													m_startTime = {t};
													m_type = "EMKT_POINT";
													m_priority = 0;
													m_duration = -1;
													m_category = "EMKC_EVENT";
													m_playingDirection = "EMPD_BOTH";
													m_ignoreWhenSkipping = false;
													m_keyProp = {{
														m_class = "arkMidnightKeyPropEvent";
														m_object = {{
															m_event = "ARK_MDNT_MSG_GRAPPLE_CAST_BEAM";
														}}
													}}
													m_keyCurves = NULL;
												}}
											}}
											m_handle = {{
												sceneUID = 1;
												groupUID = 1;
												trackType = "EMTT_EVENT";
											}}
											m_trackType = "EMTT_EVENT";
										}}
									}}
"""


def stock_read(st, path):
    for g in CATALOGS:
        if path in st.index(g):
            return st.read(g, path).decode("utf-8").replace("\r\n", "\n")  # (stock decls: CRLF)
    raise SystemExit("not in the stock catalogs: " + path)


def write(rel, text):
    p = os.path.join(MOD, "files", rel)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    with open(p, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print("wrote", rel, len(text))


def add_event_track(text):
    # the one group's track list: "m_lTracks = {\n<tabs>num = N;" ... its closing brace at the same indent
    m = re.search(r"(\n(\t+)m_lTracks = \{\n\t+num = )(\d+);", text)
    assert m, "no track list"
    indent, n = m.group(2), int(m.group(3))
    assert "EMTT_EVENT" not in text
    close = text.index("\n" + indent + "}", m.end())  # end of m_lTracks
    text = text[:close + 1] + EVENT_TRACK.format(n=n, t=CAST_AT) + text[close + 1:]
    return text[:m.start(3)] + str(n + 1) + text[m.end(3):]


def main():
    st = Stock()
    for part, src in (("in", "in"), ("loop", "loop"), ("out", "out")):
        text = stock_read(st, SRC.format(src))
        if part == "out":
            text = add_event_track(text)
        write(DST.format(part), text)
    # the power: our scenes for standing and sliding alike (Mesmerize has no slide variants), Mesmerize's cancel sound
    p = os.path.join(MOD, "files", POWER)
    text = open(p, encoding="utf-8").read()
    text = re.sub(r"\t\tm_(in|loop|out)Midnight(Slide)? = \"[^\"]*\";\n", "", text)
    text = re.sub(r"\t\tm_cancelSoundEvent = \"[^\"]*\";\n", "", text)
    lines = "".join(f'\t\tm_{k}Midnight{s} = "{SCENE.format(k)}";\n' for k in ("in", "loop", "out") for s in ("", "Slide"))
    lines += '\t\tm_cancelSoundEvent = "sound_events/bsp/bsp_power/bsp_power_mesmerize/bsp_power_mesmerize_cast_cancel";\n'
    text = text.replace("\tedit = {\n", "\tedit = {\n" + lines, 1)
    with open(p, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print("power decl: scenes", SCENE.format("in/loop/out"))


if __name__ == "__main__":
    main()
