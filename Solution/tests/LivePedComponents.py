import json
import time
import urllib.request


def request(port, path, method="GET", body=None):
    data = None if body is None else json.dumps(body).encode()
    req = urllib.request.Request(
        f"http://127.0.0.1:{port}{path}", data=data, method=method,
        headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=20) as response:
        return json.load(response)


handles = []
try:
    for index, model in enumerate([
        "shin sonic", "Waluigi", "Thanos", "DeadpoolMovie",
        "incredibox sprunki", "moban",
    ]):
        ped = request(21170, "/entities", "POST", {
            "type": "ped", "model": model, "name": "component_regression_" + model,
            "position": {"x": 50 + index * 5, "y": 20, "z": 5},
            "rotation": {"pitch": 0, "roll": 0, "yaw": 180},
            "dynamic": False, "still": True,
        })
        handles.append(ped["id"])
    source = """
using AgentHost;
using GTA.Native;
public class Check : IAgentScenario {
    public void Start(ScenarioContext c) {
        foreach (int ped in new int[]{HANDLES}) {
            for (int component = 0; component < 12; component++) {
                int drawable = Function.Call<int>(Hash.GET_PED_DRAWABLE_VARIATION, ped, component);
                if (drawable != 0) {
                    c.Fail("ped=" + ped + " component=" + component + " drawable=" + drawable);
                    return;
                }
            }
        }
        c.Complete("PASS: six new HTTP peds have standard components");
    }
    public void Tick(ScenarioContext c) {}
    public void Stop(ScenarioContext c) {}
}
""".replace("HANDLES", ",".join(map(str, handles)))
    request(21175, "/eval", "POST", {"id": "http-component-regression", "source": source})
    deadline = time.monotonic() + 30
    while time.monotonic() < deadline:
        status = request(21175, "/status")
        if status["launchError"]:
            raise RuntimeError(status["launchError"])
        if status["lastId"] == "http-component-regression" and not status["running"] and not status["compiling"]:
            assert status["lastOutcome"].startswith("PASS:"), status
            print(status["lastOutcome"])
            break
        time.sleep(0.25)
    else:
        raise TimeoutError("Component regression scenario did not finish")
finally:
    for handle in handles:
        request(21170, f"/entities/{handle}", "DELETE")
