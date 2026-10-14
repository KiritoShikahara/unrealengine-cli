"""Game E2E: builds a small coin-collecting game (HUD, game mode, pickup, chasing AI) using only
unrealengine-cli tool calls, then play-tests it and checks the result. Needs a rendering editor.
Usage: python scripts/e2e-game.py   (from the repository root, editor open on TestProject)"""
import json
import subprocess
import sys
import time

CLI = ["./target/debug/unrealengine-cli", "--project", "TestProject", "--output", "json"]
ROOT = "/Game/CoinGame/R%d" % int(time.time())


def run(*a, ok=True):
    p = subprocess.run(CLI + list(a), capture_output=True, text=True, encoding="utf-8")
    v = json.loads(p.stdout)
    if ok and not v["ok"]:
        print("FAIL", a, json.dumps(v["error"], ensure_ascii=False)[:600])
        sys.exit(1)
    return v.get("data") if v["ok"] else v["error"]


def tool(tool_name, **args):
    return run("tool", "call", tool_name, "--args", json.dumps(args))


def node(bp, ty, member=None, graph="EventGraph", **kw):
    args = {"path": bp, "type": ty, "graph": graph, **kw}
    if member:
        args["memberName"] = member
    return tool("bp_add_node", **args)


def link(bp, a, ap, b, bpin, graph="EventGraph"):
    tool("bp_connect", path=bp, graph=graph, **{"from": f"{a['id']}.{ap}", "to": f"{b['id']}.{bpin}"})


def pin(bp, n, name, value, graph="EventGraph"):
    tool("bp_set_pin", path=bp, graph=graph, node=n["id"], pin=name, value=value)


def compile_ok(bp):
    r = run("blueprint", "compile", bp)
    if r["errors"]:
        print("COMPILE ERRORS", bp, json.dumps(r, ensure_ascii=False)[:1500])
        sys.exit(1)
    run("blueprint", "save", bp)


def entry(bp, graph):
    g = tool("bp_graph_get", path=bp, graph=graph)
    return next(n for n in g["nodes"] if n["class"] == "K2Node_FunctionEntry")


p = lambda n: f"{ROOT}/{n}"
HUD, GM, COIN, AI, ENEMY = p("WBP_CoinHud"), p("BP_CoinGM"), p("BP_Coin"), p("BP_ChaserAI"), p("BP_Chaser")
BB, BT, LEVEL = p("BB_Chase"), p("BT_Chase"), p("L_CoinGame")

# --- HUD -------------------------------------------------------------------------------------
tool("widget_create", path=HUD)
tool("widget_add", path=HUD, **{"class": "TextBlock"}, name="ScoreText")
tool("widget_layout", path=HUD, widget="ScoreText", anchor="TopLeft", position=[40, 40], size=[400, 80])
tool("widget_set_prop", path=HUD, widget="ScoreText", property="Text", value="Score: 0")
compile_ok(HUD)

# --- Game mode: Score, HUD, AddScore -----------------------------------------------------------
run("blueprint", "create", GM, "--parent", "GameModeBase")
tool("bp_add_variable", path=GM, name="Score", type="int")
tool("bp_add_variable", path=GM, name="Hud", type=HUD)
tool("bp_add_function", path=GM, name="AddScore")
compile_ok(GM)
begin = node(GM, "Event", "BeginPlay")
create = node(GM, "CreateWidget", HUD)
pc = node(GM, "CallFunction", "GetPlayerController")
link(GM, pc, "ReturnValue", create, "OwningPlayer")
link(GM, begin, "then", create, "execute")
set_hud = node(GM, "VariableSet", "Hud")
link(GM, create, "then", set_hud, "execute")
link(GM, create, "ReturnValue", set_hud, "Hud")
add_vp = node(GM, "CallFunction", "AddToViewport", memberParent="UserWidget")
link(GM, set_hud, "then", add_vp, "execute")
link(GM, create, "ReturnValue", add_vp, "self")
# AddScore: Score += 1; Hud.ScoreText.SetText("Score: N"); print marker
fe = entry(GM, "AddScore")
get_score = node(GM, "VariableGet", "Score", graph="AddScore")
add = node(GM, "CallFunction", "Add_IntInt", graph="AddScore")
link(GM, get_score, "Score", add, "A", graph="AddScore")
pin(GM, add, "B", "1", graph="AddScore")
set_score = node(GM, "VariableSet", "Score", graph="AddScore")
link(GM, fe, "then", set_score, "execute", graph="AddScore")
link(GM, add, "ReturnValue", set_score, "Score", graph="AddScore")
get_hud = node(GM, "VariableGet", "Hud", graph="AddScore")
text_w = node(GM, "VariableGet", "ScoreText", graph="AddScore", memberParent=HUD)
link(GM, get_hud, "Hud", text_w, "self", graph="AddScore")
to_text = node(GM, "CallFunction", "Conv_IntToString", graph="AddScore")
link(GM, set_score, "Score", to_text, "InInt", graph="AddScore")
concat = node(GM, "CallFunction", "Concat_StrStr", graph="AddScore")
pin(GM, concat, "A", "Score: ", graph="AddScore")
link(GM, to_text, "ReturnValue", concat, "B", graph="AddScore")
str_to_text = node(GM, "CallFunction", "Conv_StringToText", graph="AddScore")
link(GM, concat, "ReturnValue", str_to_text, "InString", graph="AddScore")
set_text = node(GM, "CallFunction", "SetText", graph="AddScore", memberParent="TextBlock")
link(GM, text_w, "ScoreText", set_text, "self", graph="AddScore")
link(GM, str_to_text, "ReturnValue", set_text, "InText", graph="AddScore")
link(GM, set_score, "then", set_text, "execute", graph="AddScore")
prt = node(GM, "CallFunction", "PrintString", graph="AddScore")
link(GM, concat, "ReturnValue", prt, "InString", graph="AddScore")
link(GM, set_text, "then", prt, "execute", graph="AddScore")
compile_ok(GM)

# --- Coin: overlap -> GameMode.AddScore -> destroy ---------------------------------------------
run("blueprint", "create", COIN, "--parent", "Actor")
tool("bp_add_component", path=COIN, **{"class": "SphereComponent"}, name="Trigger")
compile_ok(COIN)
ov = node(COIN, "ComponentEvent", "Trigger", delegate="OnComponentBeginOverlap")
gm = node(COIN, "CallFunction", "GetGameMode")
cast = node(COIN, "Cast", GM)
link(COIN, ov, "then", cast, "execute")
link(COIN, gm, "ReturnValue", cast, "Object")
call_add = node(COIN, "CallFunction", "AddScore", memberParent=GM)
link(COIN, cast, "then", call_add, "execute")
link(COIN, cast, "AsBP Coin GM", call_add, "self")
destroy = node(COIN, "CallFunction", "K2_DestroyActor")
link(COIN, call_add, "then", destroy, "execute")
compile_ok(COIN)

# --- AI: blackboard, tree, controller, enemy -----------------------------------------------------
tool("blackboard_create", path=BB, keys=[{"name": "Target", "type": "Object", "baseClass": "Actor"}])
tool("behavior_tree_create", path=BT, blackboard=BB)
sel = tool("bt_add_node", path=BT, parent="root", kind="composite", **{"class": "Selector"})
tool("bt_add_node", path=BT, parent=sel["id"], kind="task", **{"class": "MoveTo"}, properties={"BlackboardKey": "Target", "AcceptableRadius": 50})
run("asset", "save", BT)
run("asset", "save", BB)
run("blueprint", "create", AI, "--parent", "AIController")
b2 = node(AI, "Event", "BeginPlay")
rbt = node(AI, "CallFunction", "RunBehaviorTree")
pin(AI, rbt, "BTAsset", BT)
link(AI, b2, "then", rbt, "execute")
bbc = node(AI, "CallFunction", "GetBlackboard", memberParent="AIBlueprintHelperLibrary")
self_n = node(AI, "Self")
link(AI, self_n, "self", bbc, "Target")
setobj = node(AI, "CallFunction", "SetValueAsObject", memberParent="BlackboardComponent")
link(AI, bbc, "ReturnValue", setobj, "self")
lit = node(AI, "CallFunction", "MakeLiteralName")
pin(AI, lit, "Value", "Target")
link(AI, lit, "ReturnValue", setobj, "KeyName")
pawn = node(AI, "CallFunction", "GetPlayerPawn")
link(AI, pawn, "ReturnValue", setobj, "ObjectValue")
link(AI, rbt, "then", setobj, "execute")
compile_ok(AI)
run("blueprint", "create", ENEMY, "--parent", "Character")
compile_ok(ENEMY)
run("prop", "set", ENEMY, "AIControllerClass", AI + "." + AI.split("/")[-1] + "_C")
run("prop", "set", ENEMY, "AutoPossessAI", "PlacedInWorldOrSpawned")
run("blueprint", "compile", ENEMY)
run("blueprint", "save", ENEMY)

# --- Level ---------------------------------------------------------------------------------------
run("level", "new", LEVEL, "--template", "/Engine/Maps/Templates/Template_Default")
starts = tool("actor_list", classFilter="PlayerStart")
s = starts[0]["location"]
yaw = starts[0]["rotation"]["yaw"]
print("player start", s, "yaw", yaw)
import math
fx, fy = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
coin_at = [s["x"] + fx * 400, s["y"] + fy * 400, s["z"]]
tool("actor_spawn", actorClass=COIN, location=coin_at, label="Coin1")
enemy_at = [s["x"] - fy * 900, s["y"] + fx * 900, s["z"]]
tool("actor_spawn", actorClass=ENEMY, location=enemy_at, label="Chaser")
tool("nav_bounds_add", location=[s["x"], s["y"], 0], extent=[4000, 4000, 1000])
run("prop", "set", "WorldSettings", "DefaultGameMode", GM + "." + GM.split("/")[-1] + "_C")
run("level", "save")
for _ in range(40):
    if tool("nav_path", **{"from": enemy_at, "to": [s["x"], s["y"], s["z"]]})["valid"]:
        break
    time.sleep(0.5)

# --- Play-test -------------------------------------------------------------------------------------
run("pie", "start")
before = run("pie", "get", "player")
enemy_before = run("pie", "get", "Chaser")
run("pie", "input", "W", "--seconds", "0.8")
run("pie", "wait", "--seconds", "1.5")
after = run("pie", "get", "player")
coins = [a for a in run("pie", "actors", "--class", "BP_Coin") if a["class"] == "BP_Coin_C"]
gm_actor = run("pie", "actors", "--class", "BP_CoinGM")
score = run("pie", "get", gm_actor[0]["name"], "Score")["properties"]["Score"] if gm_actor else None
enemy_after = run("pie", "get", "Chaser")
shot = run("pie", "screenshot")
run("pie", "stop")


def d(a, b):
    return math.dist([a["x"], a["y"], a["z"]], [b["x"], b["y"], b["z"]])


report = {
    "playerMoved": round(d(before["location"], after["location"])),
    "coinsLeft": len(coins),
    "score": score,
    "enemyMoved": round(d(enemy_before["location"], enemy_after["location"])),
    "enemyToPlayerBefore": round(d(enemy_before["location"], before["location"])),
    "enemyToPlayerAfter": round(d(enemy_after["location"], after["location"])),
    "screenshot": shot["path"],
    "blank": shot["blank"],
}
print(json.dumps(report, indent=1))
checks = {
    "player moved": report["playerMoved"] > 300,
    "coin collected": report["coinsLeft"] == 0,
    "score is 1": report["score"] == "1",
    "enemy chased the player": report["enemyToPlayerAfter"] < report["enemyToPlayerBefore"] / 2,
    "screenshot not blank": not report["blank"],
    "screenshot includes UI": shot.get("source") == "slate",
}
failed = [k for k, v in checks.items() if not v]
print("FAILED: " + ", ".join(failed) if failed else "ALL CHECKS PASSED")
sys.exit(1 if failed else 0)
