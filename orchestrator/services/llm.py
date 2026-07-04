"""
MiniMax LLM streaming client.

Sadece MiniMax desteklenir (ZeroClaw kaldırıldı). Sohbet geçmişi IP-keyed
dict'te tutulur, MAX_HISTORY_TURNS ile eski mesajlar otomatik düşer.

Streaming format (SSE benzeri):
  data: {"choices":[{"delta":{"content":"..."}}]}
  data: {"choices":[{"finish_reason":"stop",...}]}

Boş yanıtta user mesajı history'den geri alınır — sonraki turda bağlam
bozulmasın.
"""

import json
from typing import AsyncGenerator

import httpx

from orchestrator.config import (
    MAX_HISTORY_TURNS,
    MINIMAX_API_KEY,
    MINIMAX_GROUP_ID,
    SYSTEM_PROMPT,
)


# ─── Sohbet Geçmişi Deposu ───────────────────────────────────────────────────
# IP-keyed dict. Reconnect sonrası aynı IP → aynı history. Farklı IP → farklı
# kullanıcı isolation. Kullanıcı talep ederse Redis/SQLite'e taşınabilir.
conversation_histories: dict[str, list] = {}


def get_history(session_id: str) -> list:
    """session_id (IP) için history listesi; yoksa boş oluşturur."""
    if session_id not in conversation_histories:
        conversation_histories[session_id] = []
    return conversation_histories[session_id]


def clear_history(session_id: str) -> None:
    """session_id history'sini sil (test/admin için)."""
    conversation_histories.pop(session_id, None)


# ─── MiniMax Streaming Chat ──────────────────────────────────────────────────

async def chat_stream(session_id: str, user_text: str) -> AsyncGenerator[str, None]:
    """MiniMax-M2.7 streaming chat — düşük gecikme için OpenAI uyumlu API.

    session_id = client IP. Reconnect sonrası aynı IP → aynı history.

    Yields:
        Her delta token (parça parça) → core/pipeline.py cümle-bazlı
        TTS tetiklemesi için kullanır.

    Hata durumunda log yazar, history'ye boş user mesajı eklemez.
    """
    history = get_history(session_id)
    history.append({"role": "user", "content": user_text})

    url = f"https://api.minimax.io/v1/text/chatcompletion_v2?GroupId={MINIMAX_GROUP_ID}"
    headers = {
        "Authorization": f"Bearer {MINIMAX_API_KEY}",
        "Content-Type": "application/json",
    }

    messages = [{"role": "system", "content": SYSTEM_PROMPT}]
    messages.extend(history[-MAX_HISTORY_TURNS:])

    payload = {
        "model": "MiniMax-M2.7",
        "messages": messages,
        "stream": True,
        "max_tokens": 300,
        "temperature": 0.7,
    }

    full_response = ""
    debug_count = 0

    try:
        async with httpx.AsyncClient(timeout=60) as client:
            async with client.stream("POST", url, headers=headers, json=payload) as resp:
                if resp.status_code != 200:
                    error_text = await resp.aread()
                    print(f"[LLM] HTTP {resp.status_code}: {error_text[:300]}")
                    if resp.status_code == 529:
                        print("[LLM] Server overloaded, waiting 5s...")
                        await __import__("asyncio").sleep(5)
                    # Boş user mesajını geri al
                    if history and history[-1]["role"] == "user":
                        history.pop()
                    return

                async for line in resp.aiter_lines():
                    line = line.strip()
                    if not line or line == "data: ":
                        continue
                    if line.startswith("data:"):
                        line = line[5:].strip()
                    if not line or line == "[DONE]" or line == "{}":
                        continue

                    debug_count += 1
                    if debug_count <= 3:
                        print(f"[LLM-DEBUG] Line #{debug_count}: {repr(line[:150])}")

                    try:
                        data = json.loads(line)

                        if data.get("type") == "error":
                            print(f"[LLM] Stream error: "
                                  f"{data.get('error', {}).get('message', 'Unknown')}")
                            break

                        content = None
                        if "choices" in data and data["choices"]:
                            choice = data["choices"][0]
                            if "message" in choice and "content" in choice["message"]:
                                content = choice["message"]["content"]
                            elif "delta" in choice and "content" in choice["delta"]:
                                content = choice["delta"]["content"]
                            elif "text" in choice:
                                content = choice["text"]
                            if choice.get("finish_reason") and not content:
                                break

                        if content:
                            # Bazı server'lar delta'da cumulative content yollar;
                            # already-eklediğimiz kısmı atla (yankı önleme)
                            if full_response.endswith(content):
                                continue
                            full_response += content
                            yield content

                    except json.JSONDecodeError:
                        continue
                    except (KeyError, IndexError, TypeError) as e:
                        print(f"[LLM] Parse error: {e}")
                        continue

    except httpx.RequestError as e:
        print(f"[LLM] Network error: {e}")
    except Exception as e:
        print(f"[LLM] Unexpected error: {type(e).__name__}: {e}")
        import traceback
        traceback.print_exc()

    if full_response:
        full_response = full_response.strip()
        print(f"[LLM] Full response: '{full_response}' ({len(full_response)} chars)")
        history.append({"role": "assistant", "content": full_response})
        print(f"[LLM] History: {len(history)} messages (ip={session_id})")
    else:
        print("[LLM] Empty response")
        if history and history[-1]["role"] == "user":
            history.pop()


# ─── Tool Calling (Agentic Runtime için) ────────────────────────────────────
# OpenAI function_calling format: LLM ya text ya da tool_calls listesi döner.
# agent.py tool_calls dispatch eder, sonuçları LLM'e geri verir, doğal dil
# cevap üretmesini sağlar.

async def chat_with_tools(
    session_id: str,
    user_text: str,
    tools: list[dict],
) -> dict:
    """Tool calling destekli MiniMax çağrısı.

    Args:
        session_id: IP-based history key
        user_text:   Kullanıcı girdisi (fuzzy match sonrası)
        tools:       OpenAI format tool schema listesi
                     [{type: "function", function: {name, description, parameters}}, ...]

    Returns:
        dict: {"content": str | None, "tool_calls": list[dict] | None, "history": list}
        - content: LLM'in text yanıtı (yoksa None)
        - tool_calls: [{id, name, arguments}] listesi (yoksa None)
        - history: güncel history (agent run sonrası kayıt için)
    """
    history = get_history(session_id)
    history.append({"role": "user", "content": user_text})

    url = f"https://api.minimax.io/v1/text/chatcompletion_v2?GroupId={MINIMAX_GROUP_ID}"
    headers = {
        "Authorization": f"Bearer {MINIMAX_API_KEY}",
        "Content-Type": "application/json",
    }

    messages = [{"role": "system", "content": SYSTEM_PROMPT}]
    messages.extend(history[-MAX_HISTORY_TURNS:])

    payload = {
        "model": "MiniMax-M2.7",
        "messages": messages,
        "stream": False,  # tool calling non-streaming daha temiz parse
        "max_tokens": 500,
        "temperature": 0.7,
        "tools": tools,
        "tool_choice": "auto",  # LLM kendi karar versin
    }

    try:
        async with httpx.AsyncClient(timeout=60) as client:
            resp = await client.post(url, headers=headers, json=payload)

        if resp.status_code != 200:
            print(f"[LLM-Tool] HTTP {resp.status_code}: {resp.text[:300]}")
            if history and history[-1]["role"] == "user":
                history.pop()
            return {"content": None, "tool_calls": None, "history": history}

        data = resp.json()
        choice = data.get("choices", [{}])[0]
        message = choice.get("message", {})

        content = message.get("content")
        raw_tool_calls = message.get("tool_calls")

        tool_calls: list[dict] | None = None
        if raw_tool_calls:
            tool_calls = []
            for tc in raw_tool_calls:
                tool_calls.append({
                    "id":        tc.get("id"),
                    "name":      tc.get("function", {}).get("name"),
                    "arguments": tc.get("function", {}).get("arguments", "{}"),
                })

        # History'ye tool_call mesajı ekle (OpenAI multi-turn tool call pattern)
        assistant_msg = {"role": "assistant", "content": content}
        if tool_calls:
            assistant_msg["tool_calls"] = [
                {
                    "id":       tc["id"],
                    "type":     "function",
                    "function": {"name": tc["name"], "arguments": tc["arguments"]},
                }
                for tc in tool_calls
            ]
        history.append(assistant_msg)

        if content:
            print(f"[LLM-Tool] Content: '{content[:100]}'")
        if tool_calls:
            for tc in tool_calls:
                print(f"[LLM-Tool] Call: {tc['name']}({tc['arguments'][:80]})")

        return {"content": content, "tool_calls": tool_calls, "history": history}

    except Exception as e:
        print(f"[LLM-Tool] Error: {type(e).__name__}: {e}")
        import traceback
        traceback.print_exc()
        if history and history[-1]["role"] == "user":
            history.pop()
        return {"content": None, "tool_calls": None, "history": history}


async def chat_with_tool_results(
    session_id: str,
    tool_results: list[dict],
) -> dict:
    """Tool call sonuçlarını LLM'e geri ver, doğal dil cevap al.

    Args:
        session_id:   IP-based history key
        tool_results: [{tool_call_id, name, result}, ...] — agent.run'dan gelir

    Returns:
        dict: {"content": str, "history": list} — final doğal dil yanıtı
    """
    history = get_history(session_id)

    # tool_results'ı OpenAI format'ına çevir
    for tr in tool_results:
        history.append({
            "role":         "tool",
            "tool_call_id": tr["tool_call_id"],
            "name":         tr["name"],
            "content":      tr["result"] if isinstance(tr["result"], str)
                            else json.dumps(tr["result"], ensure_ascii=False),
        })

    url = f"https://api.minimax.io/v1/text/chatcompletion_v2?GroupId={MINIMAX_GROUP_ID}"
    headers = {
        "Authorization": f"Bearer {MINIMAX_API_KEY}",
        "Content-Type": "application/json",
    }

    messages = [{"role": "system", "content": SYSTEM_PROMPT}]
    messages.extend(history[-MAX_HISTORY_TURNS:])

    payload = {
        "model": "MiniMax-M2.7",
        "messages": messages,
        "stream": False,
        "max_tokens": 300,
        "temperature": 0.7,
    }

    try:
        async with httpx.AsyncClient(timeout=60) as client:
            resp = await client.post(url, headers=headers, json=payload)

        if resp.status_code != 200:
            print(f"[LLM-Tool-Result] HTTP {resp.status_code}: {resp.text[:300]}")
            return {"content": "Tool sonuçlarını işleyemedim.", "history": history}

        data = resp.json()
        content = (data.get("choices", [{}])[0]
                       .get("message", {})
                       .get("content", "")
                       .strip())

        if content:
            history.append({"role": "assistant", "content": content})
            print(f"[LLM-Tool-Result] Final: '{content[:120]}'")
        return {"content": content or "İşlem tamamlandı.", "history": history}

    except Exception as e:
        print(f"[LLM-Tool-Result] Error: {type(e).__name__}: {e}")
        return {"content": "Tool sonuçlarını işlerken hata oluştu.", "history": history}
