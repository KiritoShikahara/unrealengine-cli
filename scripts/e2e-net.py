"""Multiplayer E2E: a two-player arena built with unrealengine-cli tool calls only (replicated character movement, a
RepNotify score raised by a reliable Server RPC from the owning client), play-tested as a listen server with one
client, then again under simulated latency. Needs a rendering editor on TestProject.
Usage: python scripts/e2e-net.py (from the repository root)."""
import json
import math
import subprocess
import sys
import time

CLI = ["./target/debug/unrealengine-cli", "--project", "TestProject", "--output", "json"]
ROOT = "/Game/CoinGame/N%d" % int(time.time())


def run(*a, ok=True):
    p = subprocess.run(CLI + list(a), capture_output=True, text=True, encoding="utf-8")
    v = json.loads(p.stdout)
    if ok and not v["ok"]:
        print("FAIL", a, json.dumps(v["error"], ensure_ascii=False)[:800])
        sys.exit(1)
    return v["data"] if v["ok"] else v


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
        print("COMPILE ERRORS", bp, json.dumps(r, ensure_ascii=False)[:2000])
        sys.exit(1)
    run("blueprint", "save", bp)


def cls(path):
    return path + "." + path.split("/")[-1] + "_C"


def heroes(world):
    return [a for a in run("pie", "actors", "--world", world, "--class", "BP_NetHero") if a["class"] == "BP_NetHero_C"]


def score(world, actor):
    return int(run("pie", "get", actor, "Score", "--world", world)["properties"]["Score"])


p = lambda n: f"{ROOT}/{n}"
IA_MOVE, IA_SCORE, IMC = p("IA_Move"), p("IA_Score"), p("IMC_Net")
HERO, GM, LEVEL = p("BP_NetHero"), p("BP_NetGM"), p("L_Arena")

# --- Input --------------------------------------------------------------------------------------
tool("input_action_create", path=IA_MOVE, valueType="Axis2D")
tool("input_action_create", path=IA_SCORE, valueType="Boolean")
tool("mapping_context_create", path=IMC)
tool("mapping_add", context=IMC, action=IA_MOVE, key="W", modifiers=["Swizzle"])
tool("mapping_add", context=IMC, action=IA_SCORE, key="SpaceBar", triggers=["Pressed"])
run("asset", "save", IMC)

# --- Hero: replicated character; Score is RepNotify; SpaceBar -> Server RPC adds 10 ----------------
run("blueprint", "create", HERO, "--parent", "Character")
tool("bp_add_variable", path=HERO, name="Score", type="int")
rep = run("blueprint", "replicate", HERO, "Score", "--notify")
on_rep = rep["notifyFunction"]
entry = next(n for n in tool("bp_graph_get", path=HERO, graph=on_rep)["nodes"] if n["class"] == "K2Node_FunctionEntry")
say = node(HERO, "CallFunction", "PrintString", graph=on_rep)
link(HERO, entry, "then", say, "execute", graph=on_rep)
pin(HERO, say, "InString", "NET_SCORE", graph=on_rep)

begin = node(HERO, "Event", "BeginPlay")
pc = node(HERO, "CallFunction", "GetPlayerController")
sub_node = node(HERO, "Node", "K2Node_GetSubsystemFromPC",
                properties={"CustomClass": "/Script/EnhancedInput.EnhancedInputLocalPlayerSubsystem"})
link(HERO, pc, "ReturnValue", sub_node, "PlayerController")
addctx = node(HERO, "CallFunction", "AddMappingContext", memberParent="EnhancedInputLocalPlayerSubsystem")
link(HERO, begin, "then", addctx, "execute")
link(HERO, sub_node, "ReturnValue", addctx, "self")
pin(HERO, addctx, "MappingContext", IMC)

move = node(HERO, "InputAction", IA_MOVE)
v2 = node(HERO, "BreakStruct", "Vector2D")
link(HERO, move, "ActionValue", v2, "InVec")
fwd = node(HERO, "CallFunction", "GetActorForwardVector")
mv = node(HERO, "CallFunction", "AddMovementInput")
link(HERO, move, "Triggered", mv, "execute")
link(HERO, fwd, "ReturnValue", mv, "WorldDirection")
link(HERO, v2, "Y", mv, "ScaleValue")

server_score = node(HERO, "CustomEvent", "ServerAddScore")
run("blueprint", "rpc", HERO, "ServerAddScore", "--type", "server", "--reliable")
get_s = node(HERO, "VariableGet", "Score")
add = node(HERO, "CallFunction", "Add_IntInt")
link(HERO, get_s, "Score", add, "A")
pin(HERO, add, "B", "10")
set_s = node(HERO, "VariableSet", "Score")
link(HERO, server_score, "then", set_s, "execute")
link(HERO, add, "ReturnValue", set_s, "Score")
press = node(HERO, "InputAction", IA_SCORE)
call_rpc = node(HERO, "CallFunction", "ServerAddScore", memberParent=cls(HERO))
link(HERO, press, "Started", call_rpc, "execute")
compile_ok(HERO)

# --- Game mode and level -------------------------------------------------------------------------
run("blueprint", "create", GM, "--parent", "GameModeBase")
compile_ok(GM)
run("prop", "set", GM, "DefaultPawnClass", cls(HERO))
compile_ok(GM)
run("level", "new", LEVEL, "--template", "/Engine/Maps/Templates/Template_Default")
run("prop", "set", "WorldSettings", "DefaultGameMode", cls(GM))
run("level", "save")

# --- Play-test: listen server + 1 client ----------------------------------------------------------
started = run("pie", "start", "--players", "2", "--net-mode", "listen")
worlds = [w["id"] for w in run("pie", "worlds")]
# The client's pawn is possessed and its input set up shortly after it joins.
run("pie", "wait", "--seconds", "1")
client_pawn = run("pie", "get", "player", "--world", "client:1")
host_pawn = run("pie", "get", "player", "--world", "server")
before = client_pawn["location"]

# Movement: client 1 walks forward; the server and the client agree on where it is.
run("pie", "input", "W", "--seconds", "1", "--world", "client:1")
run("pie", "wait", "--seconds", "1")
after_client = run("pie", "get", "player", "--world", "client:1")["location"]
on_server = [h["location"] for h in heroes("server")]
dist = lambda a, b: math.sqrt(sum((a[k] - b[k]) ** 2 for k in "xyz"))
moved = dist(before, after_client)
server_gap = min(dist(after_client, s) for s in on_server)

# Score: SpaceBar on client 1 -> Server RPC -> Score replicates back -> OnRep on the client.
since = run("logs", "--limit", "1")["nextSince"]
run("pie", "input", "SpaceBar", "--seconds", "0.1", "--world", "client:1")
synced = run("net", "assert", "BP_NetHero_C", "Score", "--timeout", "3", ok=False)
run("pie", "wait", "--seconds", "1")
client_score = score("client:1", "player")
host_score = score("server", "player")
lines = run("logs", "--since", str(since), "--category", "LogBlueprintUserMessages")["lines"]
onrep_on_client = any("Client 1: NET_SCORE" in l["text"] for l in lines)

# Latency: with 300 ms simulated lag the next score reaches the client later, but it does arrive.
run("net", "emulate", "--latency", "300")
t0 = time.time()
run("pie", "input", "SpaceBar", "--seconds", "0.1", "--world", "client:1")
lagged = None
while time.time() - t0 < 8:
    if score("client:1", "player") == 20:
        lagged = time.time() - t0
        break
    time.sleep(0.05)
run("net", "emulate", "--off")
run("pie", "stop")

report = {
    "worlds": worlds,
    "clientMoved": round(moved),
    "serverPositionGap": round(server_gap, 1),
    "clientScore": client_score,
    "hostScore": host_score,
    "onRepOnClient": onrep_on_client,
    "secondsUnderLag": None if lagged is None else round(lagged, 2),
}
print(json.dumps(report, indent=1))
checks = {
    "listen server and one client": worlds == ["server", "client:1"],
    "client 1 moves (owning client input)": report["clientMoved"] > 100,
    "the server sees the same position (movement replication)": report["serverPositionGap"] < 50,
    "Server RPC raised client 1's score": client_score == 10,
    "the host's score is untouched": host_score == 0,
    "RepNotify ran on the client": onrep_on_client,
    "the score arrives under 300 ms lag, later": lagged is not None and lagged >= 0.3,
}
failed = [k for k, v in checks.items() if not v]
print("FAILED: " + ", ".join(failed) if failed else "ALL CHECKS PASSED")
sys.exit(1 if failed else 0)
