"""
Agentic runtime — LangGraph tabanlı orchestrator.

LLM'e tool listesi verir; LLM karar verir → ilgili tool çağrılır → sonuç
LLM'e geri gider → doğal dil cevap. Multi-step reasoning destekler
(örn. "yarın hava yağmurluysa müziği durdur" → weather + spotify zincir).

Mimari (LangGraph StateGraph):
  START → call_llm → [tool_calls?] → dispatch → call_llm → ... → END
                                ↓
                            (max 3 iter guard)

Modüller:
  - services/*.py → TOOL_SCHEMA_* (OpenAI function_calling format)
  - services/*.py → tool fonksiyonları (async def)
  - services/agent.py → keşif + dispatch + StateGraph
  - core/pipeline.py → services.agent.run(session_id, user_text) çağrısı

Yeni tool eklemek: services/yeni_tool.py yaz, TOOL_SCHEMA_YENI dict + async
def yeni_tool() ekle. _TOOL_MODULES listesine modülü ekle, agent otomatik
keşfeder, LLM otomatik kullanabilir.
"""

import json
from typing import Any, Literal

from langgraph.graph import END, START, StateGraph

from orchestrator.services import llm as llm_svc
from orchestrator.services import matter, spotify, time as time_svc, weather


# ─── Tool Registry (otomatik keşif) ─────────────────────────────────────────

# services/ altındaki tool modülleri. Yeni tool ekle → buraya modülü ekle,
# _discover_tools otomatik TOOL_SCHEMA_ dict'lerini ve async fonksiyonları
# toplar. Atomic tasarım prensibi: tool modülleri birbirinden bağımsız.
_TOOL_MODULES = [weather, time_svc, matter, spotify]

TOOL_SCHEMAS: list[dict] = []
# name (örn. "led.toggle") → async callable (services/<module>.toggle)
TOOL_REGISTRY: dict[str, Any] = {}


def _discover_tools() -> None:
    """services/ modüllerinden TOOL_SCHEMA_ dict'lerini ve fonksiyonları topla.

    Her schema için:
      schema["function"]["name"] = "module.function_name"
      → registry["module.function_name"] = module.function_name

    Schema adı ile fonksiyon adı birebir aynı olmalı (OpenAI tool calling
    standardı). Bu yüzden schema adının son kısmı = fonksiyon adı.
    """
    for module in _TOOL_MODULES:
        module_name = module.__name__.split(".")[-1]
        for attr_name in dir(module):
            if not attr_name.startswith("TOOL_SCHEMA_"):
                continue
            schema = getattr(module, attr_name)
            tool_name = schema["function"]["name"]
            TOOL_SCHEMAS.append(schema)
            func_attr = tool_name.split(".")[-1]
            func = getattr(module, func_attr, None)
            if func is not None and callable(func):
                TOOL_REGISTRY[tool_name] = func
            else:
                print(f"[Agent] Warning: schema {tool_name} ama "
                      f"{module_name}.{func_attr} bulunamadı")


_discover_tools()


# ─── Tool Dispatch ──────────────────────────────────────────────────────────

async def dispatch(tool_calls: list[dict]) -> list[dict]:
    """LLM'in tool_call listesini al, fonksiyonları çalıştır, sonuçları döndür.

    Args:
        tool_calls: chat_with_tools'tan gelen [{id, name, arguments}, ...]

    Returns:
        [{tool_call_id, name, result: str|dict}, ...]
        dispatch_node bu sonuçları state'e yazar, bir sonraki call_llm_node
        chat_with_tool_results ile LLM'e geri verir.
    """
    results: list[dict] = []
    for tc in tool_calls:
        name = tc.get("name")
        tc_id = tc.get("id")
        args_raw = tc.get("arguments", "{}")

        # arguments JSON string → dict
        try:
            args = json.loads(args_raw) if isinstance(args_raw, str) else args_raw
        except json.JSONDecodeError:
            print(f"[Agent] Invalid JSON args for {name}: {args_raw[:80]}")
            args = {}

        func = TOOL_REGISTRY.get(name)
        if func is None:
            print(f"[Agent] Unknown tool: {name}")
            results.append({
                "tool_call_id": tc_id,
                "name":         name,
                "result":       f"Tool not found: {name}",
            })
            continue

        try:
            result = await func(**args)
            print(f"[Agent] {name}{args} → {str(result)[:100]}")
            results.append({
                "tool_call_id": tc_id,
                "name":         name,
                "result":       result,
            })
        except Exception as e:
            print(f"[Agent] Tool {name} error: {type(e).__name__}: {e}")
            results.append({
                "tool_call_id": tc_id,
                "name":         name,
                "result":       f"Tool error: {e}",
            })
    return results


# ─── LangGraph State Machine ────────────────────────────────────────────────
# Akış: START → call_llm → [tool_calls? & iter < MAX] → dispatch → call_llm → END
# Her call_llm node'unda LLM çağrılır: ilki user_text+tools ile, sonrakiler
# tool_results ile (history üzerinden). MAX_ITERATIONS guard sonsuz döngüyü
# önler.

from typing import TypedDict


class AgentState(TypedDict, total=False):
    """LangGraph state — graph boyunca her node'da güncellenir."""
    session_id: str      # IP-keyed history
    user_text: str       # İlk kullanıcı girdisi
    tool_calls: list     # Son LLM cevabındaki tool_call listesi (boş = final)
    tool_results: list   # dispatch_node çıktısı → sonraki call_llm'e gider
    final_text: str      # LLM'in doğal dil cevabı (TTS'e gidecek)
    iteration: int       # call_llm çağrı sayısı (MAX_ITERATIONS guard)


MAX_ITERATIONS = 3  # "yarın yağmurluysa müziği durdur" gibi zincirler için


# ─── Graph Nodes ────────────────────────────────────────────────────────────

async def call_llm_node(state: AgentState) -> dict:
    """LLM çağır — ilk iter'de user_text+tools, sonrakilerde tool_results ile.

    Mevcut llm_svc history-based çalışır (IP-keyed dict). chat_with_tools
    ilk user_text'i history'ye ekler, chat_with_tool_results tool result'ları
    ekler. StateGraph tüm multi-turn konuşmayı orkestre eder.
    """
    session_id = state["session_id"]
    iteration = state.get("iteration", 0)

    if iteration == 0:
        # İlk çağrı: user_text + tools
        response = await llm_svc.chat_with_tools(
            session_id, state["user_text"], TOOL_SCHEMAS
        )
    else:
        # Sonraki çağrılar: tool_results'ı history'ye ekle + final content iste
        response = await llm_svc.chat_with_tool_results(
            session_id, state.get("tool_results", [])
        )

    content = response.get("content")
    tool_calls = response.get("tool_calls") or []

    return {
        "tool_calls": tool_calls,
        "final_text": content or "",
        "iteration":  iteration + 1,
    }


async def dispatch_node(state: AgentState) -> dict:
    """Tool calls'ı çalıştır, sonuçları state'e yaz.

    Bir sonraki call_llm_node bu sonuçları chat_with_tool_results'a verir.
    """
    results = await dispatch(state["tool_calls"])
    return {"tool_results": results}


# ─── Conditional Routing ────────────────────────────────────────────────────

def should_continue(state: AgentState) -> Literal["dispatch", "end"]:
    """Tool call varsa ve iter < MAX → dispatch'e, yoksa END.

    İki guard:
      1. iteration < MAX_ITERATIONS — sonsuz tool chain önleme
      2. tool_calls boş değil — LLM final content verdi, iş bitti
    """
    if state.get("iteration", 0) >= MAX_ITERATIONS:
        return "end"
    if state.get("tool_calls"):
        return "dispatch"
    return "end"


# ─── Graph Build & Run ─────────────────────────────────────────────────────

def _build_graph():
    """StateGraph'i kur ve compile et. Modül import'unda 1 kez çalışır."""
    g = StateGraph(AgentState)
    g.add_node("call_llm", call_llm_node)
    g.add_node("dispatch", dispatch_node)
    g.add_edge(START, "call_llm")
    g.add_conditional_edges(
        "call_llm",
        should_continue,
        {"dispatch": "dispatch", "end": END},
    )
    g.add_edge("dispatch", "call_llm")
    return g.compile()


_graph = _build_graph()


async def run(session_id: str, user_text: str) -> str:
    """Agentic run — LangGraph StateGraph ile multi-step tool orchestration.

    Akış:
      START → call_llm(user_text+tools) → [if tool_calls] → dispatch
            → call_llm(tool_results) → ... → END

    Returns:
        LLM'in final doğal dil cevabı (TTS'e gidecek). Boşsa "Anladım."
    """
    initial_state: AgentState = {
        "session_id": session_id,
        "user_text":  user_text,
        "iteration":  0,
    }
    result = await _graph.ainvoke(initial_state)
    final = result.get("final_text") or ""
    return final.strip() or "Anladım."
