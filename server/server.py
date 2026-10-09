import asyncio  # 异步 I/O 支撑
import base64  # 用于 base64 编解码（TTS 返回的音频数据为 base64）
import gzip  # gzip 压缩与解压（ASR/TTS 协议使用 gzip）
import json  # JSON 编解码
import os  # 读取环境变量等操作
import re  # 正则，用于文本匹配与提取
import struct  # 用于构建/解析二进制协议头
import uuid  # 生成唯一请求 ID
from dataclasses import dataclass  # 简化数据类定义
from typing import Any  # 类型注解

import requests  # 同步 HTTP 请求库（用于 TTS 的同步调用）
import websockets  # WebSocket 服务与客户端
from openai import AsyncOpenAI  # 异步 OpenAI 客户端（封装的模型调用）

API_KEY = os.getenv("ARK_API_KEY", "")  # LLM 平台 API Key
BASE_URL = os.getenv("ARK_BASE_URL", "")  # LLM 平台 base URL
MODEL_NAME = os.getenv("ARK_MODEL_NAME", "doubao-seed-1-8-251228")  # 使用的大模型名称

client_ai = AsyncOpenAI(api_key=API_KEY, base_url=BASE_URL)  # 异步模型客户端实例

VOLC_APP_ID = os.getenv("VOLC_APP_ID", "")  # 火山引擎（Volc）应用 ID
VOLC_ACCESS_TOKEN = os.getenv("VOLC_ACCESS_TOKEN", "")  # 火山引擎访问令牌
VOLC_ASR_CLUSTER = os.getenv("VOLC_ASR_CLUSTER", "volcengine_streaming_common")  # ASR 服务集群名
VOLC_ASR_WS_URL = "wss://openspeech.bytedance.com/api/v2/asr"  # ASR WebSocket 地址
VOLC_TTS_URL = "https://openspeech.bytedance.com/api/v1/tts"  # TTS HTTP 接口地址
VOLC_TTS_CLUSTER = "volcano_tts"  # TTS 使用的集群
VOLC_TTS_VOICE = os.getenv("VOLC_TTS_VOICE", "zh_male_yangguangqingnian_moon_bigtts")  # 默认 TTS 语音
VOLC_AUDIO_RATE = 16000  # 音频采样率（Hz）
VOLC_AUDIO_BITS = 16  # 每样本位宽
VOLC_AUDIO_CHANNELS = 1  # 声道数（单声道）
VOLC_ASR_CHUNK_BYTES = 3200  # 发送给 ASR 的音频分片大小（字节）
VOLC_TTS_CHUNK_BYTES = 2048  # 下发给设备的 TTS 分片大小（字节）
PCM_BYTES_PER_SECOND = VOLC_AUDIO_RATE * (VOLC_AUDIO_BITS // 8) * VOLC_AUDIO_CHANNELS  # 每秒 PCM 字节数

ASR_HEADER_FULL_REQUEST = bytes((0x11, 0x10, 0x11, 0x00))  # ASR 协议：首包头标识
ASR_HEADER_AUDIO_MORE = bytes((0x11, 0x20, 0x01, 0x00))  # ASR 协议：中间音频包头
ASR_HEADER_AUDIO_FINAL = bytes((0x11, 0x22, 0x01, 0x00))  # ASR 协议：最终音频包头
ASR_SERVER_FULL_RESPONSE = 0x09  # ASR 服务发送完整结果的消息类型标识
ASR_SERVER_ERROR_RESPONSE = 0x0F  # ASR 服务错误消息类型标识

SCENE_STYLE_DEFAULTS: dict[str, dict[str, Any]] = {
    "focus": {
        "display_title": "????",  # 场景展示标题（占位）
        "light_mode": "processing",  # 灯光模式
        "light_color": [64, 170, 255],  # 灯光 RGB
        "light_speed": 80,  # 灯光速度
        "offline_music_id": 1,  # 离线音乐 ID
    },
    "sleep": {
        "display_title": "????",
        "light_mode": "solid",
        "light_color": [32, 96, 180],
        "light_speed": 140,
        "offline_music_id": 2,
    },
    "healing": {
        "display_title": "????",
        "light_mode": "speaking",
        "light_color": [255, 120, 64],
        "light_speed": 90,
        "offline_music_id": 3,
    },
    "default": {
        "display_title": "AI ??",  # 默认展示标题
        "light_mode": "idle",
        "light_color": [0, 180, 48],
        "light_speed": 120,
        "offline_music_id": 1,
    },
}

SCENE_PRESET_RESPONSES: dict[str, list[str]] = {
    "focus": [
        "专心投入学习吧，我会静静陪着你。",  # 专注场景预设回复
        "先把注意力收回来，慢慢进入状态。",
        "别着急，我们先安静地开始这一段。",
    ],
    "sleep": [
        "今晚先放松一点，慢慢睡。",  # 助眠场景预设回复
        "先把呼吸放轻，我陪你安静下来。",
        "别想太多，先让自己慢慢松下来。",
    ],
    "healing": [
        "别担心呀，你已经做得很好了。",  # 治愈场景预设回复
        "先缓一缓，我陪你慢慢放松下来。",
        "今天也辛苦了，先让自己舒服一点。",
    ],
    "default": [
        "场景已经切换好了。",  # 默认场景回复
        "好的，已经帮你准备好了。",
    ],
}

SCENE_GREETING_POOL_V2: dict[str, list[str]] = {
    "focus": [
        "进入专注时间了，我帮你把氛围调好了。",  # 专注场景问候池
        "专注卡已生效，慢慢把注意力收回来。",
        "我们先静下来，开始这一段专注时间。",
        "现在切到专注模式，先把节奏稳住。",
        "专注场景已经准备好，慢慢进入状态吧。",
        "灯光和音乐都就位了，开始专心吧。",
    ],
    "sleep": [
        "助眠场景已经打开，先让自己放松一点。",  # 助眠场景问候池
        "今晚先别着急，慢慢把呼吸放轻。",
        "现在切到助眠模式，我陪你安静下来。",
        "灯光已经柔和下来了，先慢慢入睡吧。",
        "助眠音乐准备好了，先让大脑歇一会儿。",
        "这一刻先放松，别让自己太紧绷。",
    ],
    "healing": [
        "治愈场景已经打开，先让心情缓一缓。",  # 治愈场景问候池
        "现在给你一点轻松的氛围，慢慢放松下来。",
        "今天辛苦了，先听点让人舒服的音乐吧。",
        "我把灯光和音乐都调柔和了，先歇一会儿。",
        "这一刻先照顾一下自己的情绪吧。",
        "别急，先把心情放松下来，我陪着你。",
    ],
    "default": [
        "场景已经切换好了。",  # 默认问候池
        "好的，已经帮你准备好了。",
        "新的互动场景已经生效。",
    ],
}

MOOD_LABELS = ("happy", "calm", "focus", "tired", "sad")
DEFAULT_MOOD_BY_SCENE = {
    "focus": "focus",
    "sleep": "calm",
    "healing": "calm",
    "default": "calm",
}

FREE_CHAT_PERSONA = """
用户是一名大四学生，正在做毕业设计。
你的回复要像一个简洁、靠谱、不会说教的桌面陪伴助手。
优先给出简短、自然、可直接语音播报的话，不要继续沿用当前刷卡场景的语气设定。
""".strip()

FREE_CHAT_STYLE: dict[str, Any] = {
    "display_title": "毕设小助手",
    "light_mode": "speaking",
    "light_color": [96, 200, 255],
    "light_speed": 100,
    "mood_label": "calm",
}

MOOD_TRIGGER_KEYWORDS: dict[str, tuple[str, ...]] = {
    "happy": ("开心", "高兴", "快乐", "兴奋", "轻松", "放松", "治愈"),
    "calm": ("平静", "安静", "舒缓", "休息", "慢一点", "冷静"),
    "focus": ("专注", "学习", "复习", "写代码", "写论文", "赶进度"),
    "tired": ("好累", "很累", "有点累", "困", "疲惫", "没精神", "想睡", "熬夜"),
    "sad": ("难过", "伤心", "烦", "烦躁", "焦虑", "压力", "崩溃", "低落", "委屈", "想哭"),
}

MUSIC_INTENT_KEYWORDS: tuple[str, ...] = (
    "音乐",
    "歌",
    "歌曲",
    "bgm",
    "听点",
    "来点",
    "推荐",
    "适合我",
    "陪我",
)


@dataclass
class SessionContext:
    device_id: str = "unknown_device"
    scene_id: str = "default"
    scene_name: str = "????"
    scene_prompt: str = ""
    offline_music_id: int = 1

    def apply_scene_event(self, payload: dict[str, Any]) -> None:
        """将场景事件里的字段合并到当前会话上下文。"""
        self.device_id = str(payload.get("device_id") or self.device_id)
        self.scene_id = str(payload.get("scene_id") or self.scene_id)
        self.scene_name = str(payload.get("scene_name") or self.scene_name)
        self.scene_prompt = str(payload.get("scene_prompt") or self.scene_prompt)
        self.offline_music_id = int(payload.get("offline_music_id") or self.offline_music_id)


def check_volc_config() -> None:
    """检查火山引擎必需配置是否存在。"""
    if not VOLC_APP_ID or not VOLC_ACCESS_TOKEN:
        raise RuntimeError("???? VOLC_APP_ID ? VOLC_ACCESS_TOKEN")


def trim_text_for_tts(text: str, limit_bytes: int = 900) -> str:
    """将文本裁剪到 TTS 可接受长度并尽量在标点处截断。"""
    text = (text or "").strip()
    if not text:
        return ""
    if len(text.encode("utf-8")) <= limit_bytes:
        return text

    best = ""
    for char in text:
        candidate = best + char
        if len(candidate.encode("utf-8")) > limit_bytes:
            break
        best = candidate

    for punctuation in ("?", "?", "?", ".", "!", "?"):
        index = best.rfind(punctuation)
        if index >= 0:
            clipped = best[: index + 1].strip()
            if clipped:
                return clipped
    return best.strip()


def post_json(url: str, payload: dict[str, Any], timeout: int = 30) -> dict[str, Any]:
    """携带鉴权头发送 JSON POST 请求并返回解析结果。"""
    check_volc_config()
    headers = {
        "Authorization": f"Bearer;{VOLC_ACCESS_TOKEN}",
        "Content-Type": "application/json",
    }
    response = requests.post(url, json=payload, headers=headers, timeout=timeout)
    response.raise_for_status()
    return response.json()


def extract_text_from_asr(payload: dict[str, Any]) -> str:
    """从 ASR 多种返回结构中提取首个可用文本。"""
    candidates: list[str] = []

    if isinstance(payload.get("text"), str):
        candidates.append(payload["text"])

    result = payload.get("result")
    if isinstance(result, str):
        candidates.append(result)
    elif isinstance(result, dict):
        for key in ("text", "utterance", "transcript"):
            value = result.get(key)
            if isinstance(value, str):
                candidates.append(value)
        for key in ("utterances", "results"):
            value = result.get(key)
            if isinstance(value, list):
                for item in value:
                    if isinstance(item, dict):
                        text = item.get("text") or item.get("utterance") or item.get("transcript")
                        if isinstance(text, str):
                            candidates.append(text)
    elif isinstance(result, list):
        for item in result:
            if isinstance(item, str):
                candidates.append(item)
            elif isinstance(item, dict):
                for key in ("text", "utterance", "transcript"):
                    value = item.get(key)
                    if isinstance(value, str):
                        candidates.append(value)

    data = payload.get("data")
    if isinstance(data, dict):
        for key in ("text", "utterance", "transcript"):
            value = data.get(key)
            if isinstance(value, str):
                candidates.append(value)

    for candidate in candidates:
        candidate = candidate.strip()
        if candidate:
            return candidate
    return ""


def pack_asr_full_request(reqid: str) -> bytes:
    """构造 ASR 首包请求并进行 gzip 压缩。"""
    payload = {
        "app": {
            "appid": VOLC_APP_ID,
            "token": VOLC_ACCESS_TOKEN,
            "cluster": VOLC_ASR_CLUSTER,
        },
        "user": {"uid": "esp32s3_nfc_box_01"},
        "audio": {
            "format": "raw",
            "codec": "raw",
            "rate": VOLC_AUDIO_RATE,
            "bits": VOLC_AUDIO_BITS,
            "channel": VOLC_AUDIO_CHANNELS,
            "language": "zh-CN",
        },
        "request": {
            "reqid": reqid,
            "sequence": 1,
            "nbest": 1,
            "workflow": "audio_in,resample,partition,vad,fe,decode,itn,nlu_punctuate",
            "show_utterances": False,
            "result_type": "full",
            "vad_signal": True,
            "start_silence_time": 5000,
            "vad_silence_time": 800,
        },
    }

    payload_bytes = json.dumps(payload, ensure_ascii=False).encode("utf-8")
    compressed = gzip.compress(payload_bytes)
    # 返回：协议头 + 4 字节大端长度 + gzip 压缩内容
    return ASR_HEADER_FULL_REQUEST + struct.pack(">I", len(compressed)) + compressed


def pack_asr_audio_request(audio_chunk: bytes, is_last: bool) -> bytes:
    """构造 ASR 音频分片包并标记是否为最后一包。"""
    compressed = gzip.compress(audio_chunk)
    header = ASR_HEADER_AUDIO_FINAL if is_last else ASR_HEADER_AUDIO_MORE
    # 返回：分片包头 + 4 字节大端长度 + gzip 压缩音频分片
    return header + struct.pack(">I", len(compressed)) + compressed


def parse_asr_ws_message(message: bytes) -> dict[str, Any] | None:
    """解析 ASR WebSocket 二进制消息并返回结构化结果。"""
    if len(message) < 4:
        raise RuntimeError("ASR ??????????")

    header_size = (message[0] & 0x0F) * 4
    message_type = (message[1] >> 4) & 0x0F
    serialization = (message[2] >> 4) & 0x0F
    compression = message[2] & 0x0F
    payload = message[header_size:]

    if message_type == ASR_SERVER_ERROR_RESPONSE:
        if len(payload) < 8:
            raise RuntimeError("ASR ????????")
        code = int.from_bytes(payload[:4], "big", signed=False)
        size = int.from_bytes(payload[4:8], "big", signed=False)
        error_message = payload[8: 8 + size].decode("utf-8", errors="ignore")
        # ASR 返回错误，抛出异常并包含错误信息
        raise RuntimeError(f"ASR ???? code={code}, message={error_message}")

    if message_type != ASR_SERVER_FULL_RESPONSE:
        return None

    if len(payload) < 4:
        raise RuntimeError("ASR ????? payload size")

    payload_size = int.from_bytes(payload[:4], "big", signed=False)
    payload_bytes = payload[4: 4 + payload_size]

    if compression == 1:
        payload_bytes = gzip.decompress(payload_bytes)
    if serialization == 1:
        # 如果序列化格式为 JSON（1），解析并返回 dict
        return json.loads(payload_bytes.decode("utf-8"))
    return {"raw": payload_bytes}


async def speech_to_text(audio_bytes: bytes) -> str:
    """将 PCM 音频流发送到 ASR 服务并返回最终识别文本。"""
    check_volc_config()
    if not audio_bytes:
        return ""

    reqid = str(uuid.uuid4())
    headers = {"Authorization": f"Bearer;{VOLC_ACCESS_TOKEN}"}
    last_response: dict[str, Any] | None = None
    final_text = ""

    async with websockets.connect(
        VOLC_ASR_WS_URL,
        additional_headers=headers,
        max_size=8 * 1024 * 1024,
        open_timeout=10,
    ) as ws:
        await ws.send(pack_asr_full_request(reqid))

        ack_message = await asyncio.wait_for(ws.recv(), timeout=10)
        if isinstance(ack_message, str):
            raise RuntimeError(f"ASR ACK ????: {ack_message}")
        ack_response = parse_asr_ws_message(ack_message)
        if ack_response is not None:
            last_response = ack_response
            print(f"[ASR] ACK: {ack_response}")

        total_chunks = (len(audio_bytes) + VOLC_ASR_CHUNK_BYTES - 1) // VOLC_ASR_CHUNK_BYTES
        for index, offset in enumerate(range(0, len(audio_bytes), VOLC_ASR_CHUNK_BYTES), start=1):
            chunk = audio_bytes[offset: offset + VOLC_ASR_CHUNK_BYTES]
            await ws.send(pack_asr_audio_request(chunk, index == total_chunks))

        while True:
            response_message = await asyncio.wait_for(ws.recv(), timeout=15)
            if isinstance(response_message, str):
                raise RuntimeError(f"ASR ??????: {response_message}")

            response = parse_asr_ws_message(response_message)
            if response is None:
                continue

            last_response = response
            text = extract_text_from_asr(response)
            sequence = response.get("sequence")
            code = response.get("code")
            message = response.get("message")
            print(f"[ASR] response code={code}, sequence={sequence}, message={message}, text={text}")

            if text:
                final_text = text
            if isinstance(sequence, int) and sequence < 0:
                break

    if final_text:
        return final_text
    if last_response is not None:
        raise RuntimeError(f"ASR ???????: {last_response}")
    raise RuntimeError("ASR ?????????")


def text_to_speech_sync(text: str) -> bytes:
    """同步调用 TTS 接口，把文本转为 PCM 音频。"""
    clean_text = trim_text_for_tts(text)
    if not clean_text:
        return b""

    reqid = str(uuid.uuid4())
    payload = {
        "app": {
            "appid": VOLC_APP_ID,
            "token": VOLC_ACCESS_TOKEN,
            "cluster": VOLC_TTS_CLUSTER,
        },
        "user": {"uid": "esp32s3_nfc_box_01"},
        "audio": {
            "voice_type": VOLC_TTS_VOICE,
            "encoding": "pcm",
            "rate": VOLC_AUDIO_RATE,
            "speed_ratio": 1.0,
            "volume_ratio": 1.0,
            "pitch_ratio": 1.0,
        },
        "request": {
            "reqid": reqid,
            "text": clean_text,
            "text_type": "plain",
            "operation": "query",
        },
    }

    response = post_json(VOLC_TTS_URL, payload, timeout=30)
    if response.get("code") != 3000 or not response.get("data"):
        raise RuntimeError(
            f"TTS ?? reqid={reqid}, code={response.get('code')}, message={response.get('message')}"
        )
    return base64.b64decode(response["data"])


async def text_to_speech(text: str) -> bytes:
    """在线程池中异步执行同步 TTS，避免阻塞事件循环。"""
    # 使用线程池把同步的 TTS 调用转为异步，防止阻塞 asyncio 事件循环
    return await asyncio.to_thread(text_to_speech_sync, text)


def extract_json_object(text: str) -> dict[str, Any]:
    """从普通文本或代码块中提取首个 JSON 对象。"""
    text = (text or "").strip()
    if not text:
        return {}

    fenced = re.findall(r"```(?:json)?\s*(\{.*?\})\s*```", text, flags=re.S)
    candidates = fenced + re.findall(r"(\{.*\})", text, flags=re.S)

    for candidate in candidates:
        try:
            parsed = json.loads(candidate)
            if isinstance(parsed, dict):
                return parsed
        except json.JSONDecodeError:
            continue
    return {}


def base_control_for_context(session: SessionContext, control_type: str, spoken_text: str) -> dict[str, Any]:
    """基于当前场景生成一份控制指令基础模板。"""
    style = SCENE_STYLE_DEFAULTS.get(session.scene_id, SCENE_STYLE_DEFAULTS["default"])
    return {
        "type": control_type,
        "scene_id": session.scene_id,
        "spoken_text": spoken_text,
        "tts_enabled": True,
        "light_mode": style["light_mode"],
        "light_color": style["light_color"],
        "light_speed": style["light_speed"],
        "offline_music_id": session.offline_music_id or style["offline_music_id"],
        "motor_mode": "stop",
        "display_title": style["display_title"],
        "mood_label": DEFAULT_MOOD_BY_SCENE.get(session.scene_id, DEFAULT_MOOD_BY_SCENE["default"]),
        "mood_music_enabled": False,
    }


def normalize_control(model_result: dict[str, Any], session: SessionContext, control_type: str, fallback_text: str) -> dict[str, Any]:
    """将模型输出校验并合并到标准控制结构中。"""
    control = base_control_for_context(session, control_type, fallback_text)
    if not model_result:
        return control

    if isinstance(model_result.get("spoken_text"), str) and model_result["spoken_text"].strip():
        control["spoken_text"] = model_result["spoken_text"].strip()
    if isinstance(model_result.get("tts_enabled"), bool):
        control["tts_enabled"] = model_result["tts_enabled"]
    if isinstance(model_result.get("light_mode"), str) and model_result["light_mode"].strip():
        control["light_mode"] = model_result["light_mode"].strip()
    if isinstance(model_result.get("light_speed"), int):
        control["light_speed"] = model_result["light_speed"]
    if isinstance(model_result.get("offline_music_id"), int) and model_result["offline_music_id"] > 0:
        control["offline_music_id"] = model_result["offline_music_id"]
    if isinstance(model_result.get("motor_mode"), str) and model_result["motor_mode"].strip():
        control["motor_mode"] = model_result["motor_mode"].strip()
    if isinstance(model_result.get("display_title"), str) and model_result["display_title"].strip():
        control["display_title"] = model_result["display_title"].strip()
    if isinstance(model_result.get("scene_id"), str) and model_result["scene_id"].strip():
        control["scene_id"] = model_result["scene_id"].strip()
    if isinstance(model_result.get("mood_label"), str):
        mood_label = model_result["mood_label"].strip().lower()
        if mood_label in MOOD_LABELS:
            control["mood_label"] = mood_label
    if isinstance(model_result.get("mood_music_enabled"), bool):
        control["mood_music_enabled"] = model_result["mood_music_enabled"]

    light_color = model_result.get("light_color")
    if isinstance(light_color, list) and len(light_color) == 3:
        try:
            control["light_color"] = [int(light_color[0]), int(light_color[1]), int(light_color[2])]
        except (TypeError, ValueError):
            pass

    return control


async def ask_llm_for_control(user_text: str, session: SessionContext, control_type: str) -> dict[str, Any]:
    """向大模型请求控制 JSON，并解析返回对象。"""
    if control_type == "reply_control":
        prompt = f"""
Return exactly one JSON object.

Required keys:
- type
- scene_id
- spoken_text
- tts_enabled
- light_mode
- light_color
- light_speed
- offline_music_id
- motor_mode
- display_title
- mood_label
- mood_music_enabled

Rules:
- spoken_text must be one short Chinese sentence for TTS, ideally under 24 Chinese characters.
- mood_label must be one of: happy, calm, focus, tired, sad.
- mood_music_enabled must be true.
- offline_music_id must be 0.
- scene_id must be "default".
- display_title should be "对话模式" or another short chat-oriented title.
- light_mode must be one of: idle, processing, solid, speaking, offline, error.
- light_color must be [r, g, b] with 0-255 integers.
- motor_mode must be stop.
- Do not reference the currently brushed NFC scene unless the user explicitly mentions it.

Persona:
- {FREE_CHAT_PERSONA}

User text:
- {user_text}
""".strip()
    else:
        prompt = f"""
Return exactly one JSON object.

Required keys:
- type
- scene_id
- spoken_text
- tts_enabled
- light_mode
- light_color
- light_speed
- offline_music_id
- motor_mode
- display_title
- mood_label
- mood_music_enabled

Rules:
- spoken_text must be one short Chinese sentence for TTS, ideally under 24 Chinese characters.
- mood_label must be one of: happy, calm, focus, tired, sad.
- mood_music_enabled should be true only for reply_control, otherwise false.
- Do not invent a song number for reply_control. Keep offline_music_id as 0 for reply_control.
- light_mode must be one of: idle, processing, solid, speaking, offline, error.
- light_color must be [r, g, b] with 0-255 integers.
- motor_mode must be stop.
- scene_id should remain aligned with the current scene.

Context:
- current_scene_id: {session.scene_id}
- current_scene_name: {session.scene_name}
- current_scene_prompt: {session.scene_prompt}
- current_scene_track: {session.offline_music_id}
- control_type: {control_type}
- user_text: {user_text}
""".strip()

    response = await client_ai.chat.completions.create(
        model=MODEL_NAME,
        messages=[
            {
                "role": "system",
                "content": (
                    "You are a strict control planner for an ESP32 desktop audio box. "
                    "Only return JSON."
                ),
            },
            {"role": "user", "content": prompt},
        ],
    )
    raw = (response.choices[0].message.content or "").strip()
    return extract_json_object(raw)


async def build_control_reply(user_text: str, session: SessionContext, control_type: str) -> dict[str, Any]:
    """统一生成控制回复，失败时退回离线控制。"""
    fallback_text = f"{session.scene_name} ?????" if control_type == "scene_control" else "????????"
    try:
        model_result = await ask_llm_for_control(user_text, session, control_type)
        control = normalize_control(model_result, session, control_type, fallback_text)
        if control_type == "reply_control":
            const_enable_mood_music = should_enable_mood_music_for_reply(user_text)
            control["scene_id"] = "default"
            control["offline_music_id"] = 0
            control["mood_music_enabled"] = const_enable_mood_music
            if not isinstance(model_result.get("light_mode"), str) or not model_result["light_mode"].strip():
                control["light_mode"] = FREE_CHAT_STYLE["light_mode"]
            if not isinstance(model_result.get("light_speed"), int):
                control["light_speed"] = FREE_CHAT_STYLE["light_speed"]
            if not (
                isinstance(model_result.get("light_color"), list)
                and len(model_result["light_color"]) == 3
            ):
                control["light_color"] = list(FREE_CHAT_STYLE["light_color"])
            if not isinstance(model_result.get("display_title"), str) or not model_result["display_title"].strip():
                control["display_title"] = FREE_CHAT_STYLE["display_title"]
            elif control["display_title"] in {"专注卡", "助眠卡", "治愈卡"}:
                control["display_title"] = FREE_CHAT_STYLE["display_title"]
            if control.get("mood_label") not in MOOD_LABELS:
                control["mood_label"] = FREE_CHAT_STYLE["mood_label"]
            if not const_enable_mood_music:
                control["display_title"] = FREE_CHAT_STYLE["display_title"]
        else:
            control["mood_music_enabled"] = False
        print(f"[LLM] control={control}")
        return control
    except Exception as exc:
        print(f"[LLM] build control failed: {exc}")
        return build_offline_control(session, f"??????????????: {exc}")


def choose_scene_preset_reply(session: SessionContext) -> str:
    """按当前场景随机挑选一条预设欢迎文案。"""
    options = SCENE_GREETING_POOL_V2.get(session.scene_id) or SCENE_GREETING_POOL_V2["default"]
    return options[uuid.uuid4().int % len(options)]


def should_enable_mood_music_for_reply(user_text: str) -> bool:
    """根据文本意图判断回复时是否开启情绪音乐。"""
    normalized = normalize_command_text(user_text)
    if not normalized:
        return False

    if any(keyword in normalized for keyword in ("你是谁", "你叫什么", "你能做什么", "介绍一下", "自我介绍")):
        return False

    has_music_intent = any(keyword in normalized for keyword in MUSIC_INTENT_KEYWORDS)
    has_emotion_intent = any(
        keyword in normalized
        for keywords in MOOD_TRIGGER_KEYWORDS.values()
        for keyword in keywords
    )
    return has_music_intent or has_emotion_intent

def normalize_command_text(text: str) -> str:
    """对口语化命令文本做清洗、同义归一和数字标准化。"""
    normalized = (text or "").strip().lower()
    if not normalized:
        return ""

    normalized = re.sub(r"[\s,，。.!！?？:：;；\"'“”‘’()（）\\-—_~～/]+", "", normalized)
    replacements = {
        "音樂": "音乐",
        "音月": "音乐",
        "音悦": "音乐",
        "音越": "音乐",
        "歌曲": "音乐",
        "來": "来",
        "播放下": "播放",
        "播一下": "播放",
        "放一下": "播放",
        "来点": "来",
        "来个": "来",
        "来首": "播放",
        "切换到": "播放",
        "切到": "播放",
        "切成": "播放",
        "继续音乐": "继续播放",
        "接着播放": "继续播放",
        "恢复播放": "继续播放",
        "暂停播放": "暂停音乐",
        "先暂停": "暂停音乐",
        "停一下": "暂停音乐",
        "停止播放": "停止音乐",
        "关掉音乐": "停止音乐",
        "专注模式": "专注音乐",
        "助眠模式": "助眠音乐",
        "治疗音乐": "治愈音乐",
    }
    for old, new in replacements.items():
        normalized = normalized.replace(old, new)

    normalized = (
        normalized.replace("第一首", "1")
        .replace("第二首", "2")
        .replace("第三首", "3")
        .replace("第1首", "1")
        .replace("第2首", "2")
        .replace("第3首", "3")
        .replace("一号", "1")
        .replace("二号", "2")
        .replace("三号", "3")
        .replace("一首", "1")
        .replace("二首", "2")
        .replace("三首", "3")
    )
    return normalized


def make_music_control(
    session: SessionContext,
    spoken_text: str,
    *,
    action: str,
    track_id: int = 0,
    tts_enabled: bool = True,
    volume_level: int | None = None,
) -> dict[str, Any]:
    """按统一字段格式构造音乐控制指令。"""
    control = base_control_for_context(session, "music_control", spoken_text)
    control["offline_music_id"] = track_id
    control["display_title"] = "音乐控制"
    control["music_action"] = action
    control["tts_enabled"] = tts_enabled
    if volume_level is not None:
        control["volume_level"] = max(0, min(30, int(volume_level)))
    return control


def extract_requested_track_id(normalized: str) -> int:
    chinese_track_map = {
        "一": 1,
        "二": 2,
        "三": 3,
        "四": 4,
        "五": 5,
        "六": 6,
        "七": 7,
        "八": 8,
        "九": 9,
        "十": 10,
        "十一": 11,
        "十二": 12,
        "十三": 13,
        "十四": 14,
        "十五": 15,
        "十六": 16,
        "十七": 17,
        "十八": 18,
        "十九": 19,
        "二十": 20,
        "二十一": 21,
        "二十二": 22,
        "二十三": 23,
        "二十四": 24,
        "二十五": 25,
    }

    digit_patterns = [
        r"音乐(\d{1,2})",
        r"第(\d{1,2})首",
        r"播放(\d{1,2})",
        r"来(\d{1,2})",
        r"放(\d{1,2})",
        r"(\d{1,2})号",
    ]
    for pattern in digit_patterns:
        match = re.search(pattern, normalized)
        if match:
            track_id = int(match.group(1))
            if 1 <= track_id <= 25:
                return track_id

    for chinese, track_id in sorted(chinese_track_map.items(), key=lambda item: len(item[0]), reverse=True):
        if any(token in normalized for token in (f"音乐{chinese}", f"第{chinese}首", f"播放{chinese}", f"来{chinese}", f"放{chinese}", f"{chinese}号")):
            return track_id

    return 0


def parse_local_music_command(text: str, session: SessionContext) -> dict[str, Any] | None:
    """解析增强版本地音乐/音量命令并生成控制指令。"""
    normalized = normalize_command_text(text)
    if not normalized:
        return None

    if any(keyword in normalized for keyword in ("音量最大", "声音最大", "最大音量", "声音开最大", "调到最大", "开大点到最大")):
        return make_music_control(session, "好的，音量调到最大。", action="volume", volume_level=24)

    if any(keyword in normalized for keyword in ("音量最小", "声音最小", "最小音量", "声音关小", "调到最小")):
        return make_music_control(session, "好的，音量调低了。", action="volume", volume_level=4)

    if any(keyword in normalized for keyword in ("音量大一点", "声音大一点", "调大音量", "音量调大", "声音调大", "开大一点", "大声一点")):
        return make_music_control(session, "好的，音量调大一点。", action="volume", volume_level=14)

    if any(keyword in normalized for keyword in ("音量小一点", "声音小一点", "调小音量", "音量调小", "声音调小", "关小一点", "小声一点")):
        return make_music_control(session, "好的，音量调小一点。", action="volume", volume_level=7)

    if any(keyword in normalized for keyword in ("暂停音乐", "暂停", "暂停一下", "先停", "停一下", "停一停", "先暂停", "先别放了")):
        return make_music_control(session, "好的，先暂停。", action="pause")

    if any(keyword in normalized for keyword in ("继续播放", "继续", "接着播", "接着放", "恢复播放", "恢复", "继续放", "接着来")):
        return make_music_control(session, "好的，继续播放。", action="resume")

    if any(keyword in normalized for keyword in ("停止音乐", "停止", "关掉音乐", "别播了", "别放了", "关掉", "停掉音乐")):
        return make_music_control(session, "好的，已经停止。", action="stop")

    if any(
        keyword in normalized
        for keyword in (
            "下一首",
            "下首",
            "下一首歌",
            "切下一首",
            "换一首",
            "换首歌",
            "来下一首",
            "下一曲",
            "往后一首",
            "后一首",
            "下1",
            "切下1",
            "来下1",
        )
    ):
        control = make_music_control(session, "好的，切到下一首。", action="next")
        control["offline_music_id"] = 0
        return control

    if any(
        keyword in normalized
        for keyword in (
            "上一首",
            "上首",
            "上一首歌",
            "切上一首",
            "回上一首",
            "上一曲",
            "前一首",
            "往前一首",
            "上1",
            "切上1",
            "回上1",
        )
    ):
        control = make_music_control(session, "好的，回到上一首。", action="previous")
        control["offline_music_id"] = 0
        return control

    if any(keyword in normalized for keyword in ("随机音乐", "随便放", "来音乐", "随便来一首", "随机来一首", "随机放一首")):
        control = make_music_control(session, "好的，随机放一首。", action="random")
        control["offline_music_id"] = 0
        return control

    if any(keyword in normalized for keyword in ("专注音乐", "来点专注音乐", "切到专注音乐", "播放专注模式")):
        control = make_music_control(session, "好的，播放专注音乐。", action="play_mood")
        control["offline_music_id"] = 0
        control["mood_label"] = "focus"
        return control

    if any(keyword in normalized for keyword in ("助眠音乐", "睡觉音乐", "来点助眠音乐", "切到助眠音乐", "播放助眠模式")):
        control = make_music_control(session, "好的，播放助眠音乐。", action="play_mood")
        control["offline_music_id"] = 0
        control["mood_label"] = "calm"
        return control

    if any(keyword in normalized for keyword in ("治愈音乐", "放松音乐", "来点治愈音乐", "切到治愈音乐", "来点放松音乐")):
        control = make_music_control(session, "好的，播放治愈音乐。", action="play_mood")
        control["offline_music_id"] = 0
        control["mood_label"] = "happy"
        return control

    play_requested = any(keyword in normalized for keyword in ("播放", "播", "放", "来"))
    if play_requested:
        track_id = extract_requested_track_id(normalized)
        if track_id > 0:
            return make_music_control(session, f"好的，播放第{track_id}首。", action="play", track_id=track_id)

    return None


def build_offline_control(session: SessionContext, reason: str) -> dict[str, Any]:
    """构建离线兜底控制指令，用于云端异常场景。"""
    base = base_control_for_context(session, "offline_control", reason)
    base["tts_enabled"] = False
    base["light_mode"] = "offline"
    base["light_color"] = [255, 80, 0]
    base["light_speed"] = 120
    base["display_title"] = "Offline Mode"
    base["mood_label"] = "sad"
    base["mood_music_enabled"] = False
    return base


async def send_control_json(websocket, control: dict[str, Any]) -> None:
    """通过 WebSocket 下发控制 JSON。"""
    # 将控制字典序列化为 JSON 发给设备
    await websocket.send(json.dumps(control, ensure_ascii=False))


async def send_tts_chunks(websocket, text: str) -> None:
    """将 TTS 音频按分片节奏下发到设备端。"""
    audio_bytes = await text_to_speech(text)
    if not audio_bytes:
        print("[TTS] Empty text, skip audio downlink.")
        return

    # 首先发送一个 TTS_BEGIN 命令，包含音频参数和总字节数
    await websocket.send(
        f"CMD:TTS_BEGIN|{VOLC_AUDIO_RATE}|{VOLC_AUDIO_BITS}|{VOLC_AUDIO_CHANNELS}|{len(audio_bytes)}|{VOLC_TTS_CHUNK_BYTES}"
    )
    for offset in range(0, len(audio_bytes), VOLC_TTS_CHUNK_BYTES):
        chunk = audio_bytes[offset: offset + VOLC_TTS_CHUNK_BYTES]
        # 下发音频分片并按音频时长睡眠以模拟实时播放节奏
        await websocket.send(chunk)
        await asyncio.sleep(len(chunk) / PCM_BYTES_PER_SECOND)
    await websocket.send("CMD:TTS_END")
    print(f"[TTS] Downlinked {len(audio_bytes)} bytes PCM.")


async def send_reply_bundle(websocket, control: dict[str, Any]) -> None:
    """按控制 JSON、文本、音频三段式发送完整回复。"""
    await send_control_json(websocket, control)
    spoken_text = str(control.get("spoken_text") or "").strip()
    if spoken_text:
        await websocket.send(spoken_text)
    if control.get("tts_enabled", True) and spoken_text:
        try:
            await send_tts_chunks(websocket, spoken_text)
        except Exception as exc:
            # 如果 TTS 下发失败，打印错误并保留文本回复（不抛出）
            print(f"[TTS] failed, keep text only: {exc}")


async def handle_scene_event(websocket, session: SessionContext, payload: dict[str, Any]) -> None:
    """处理场景切换事件并快速返回场景控制回复。"""
    session.apply_scene_event(payload)
    spoken_text = choose_scene_preset_reply(session)
    control = base_control_for_context(session, "scene_control", spoken_text)
    control["display_title"] = session.scene_name
    control["offline_music_id"] = 0
    control["mood_label"] = DEFAULT_MOOD_BY_SCENE.get(session.scene_id, DEFAULT_MOOD_BY_SCENE["default"])
    control["mood_music_enabled"] = True
    # 打印并下发场景控制：主要用于响应设备触发的场景变更
    print(f"[Scene] fast control={control}")
    await send_reply_bundle(websocket, control)


async def handle_text_query(websocket, session: SessionContext, user_text: str) -> None:
    """处理文本查询，优先命中本地命令，否则走大模型回复。"""
    command_control = parse_local_music_command(user_text, session)
    if command_control is not None:
        # 本地命令（音乐/音量等）命中，优先执行并下发
        print(f"[Command] music control={command_control}")
        if int(command_control.get("offline_music_id") or 0) > 0:
            session.offline_music_id = int(command_control["offline_music_id"])
        await send_reply_bundle(websocket, command_control)
        return

    control = await build_control_reply(user_text, session, "reply_control")
    if int(control.get("offline_music_id") or 0) > 0:
        session.offline_music_id = int(control["offline_music_id"])
    await send_reply_bundle(websocket, control)


async def handle_voice_query(websocket, session: SessionContext, audio_bytes: bytes) -> None:
    """处理语音查询：先 ASR，再复用文本处理链路。"""
    try:
        user_text = await speech_to_text(audio_bytes)
        print(f"[ASR] result: {user_text}")
    except Exception as exc:
        # ASR 失败：记录并下发离线兜底控制
        print(f"[ASR] speech_to_text failed: {exc}")
        await send_reply_bundle(websocket, build_offline_control(session, f"??????????????: {exc}"))
        return

    if not user_text:
        # ASR 未识别出文本，返回离线兜底
        await send_reply_bundle(websocket, build_offline_control(session, "????????"))
        return

    await handle_text_query(websocket, session, user_text)


async def handle_client(websocket) -> None:
    """处理单个设备连接的全生命周期消息交互。"""
    peer = websocket.remote_address
    print("\n===================================")
    print(f"[WS] New device connected: {peer}")
    print("===================================")

    session = SessionContext(device_id="esp32s3_nfc_box_01")
    recording = False
    pcm_buffer = bytearray()

    try:
        boot_control = base_control_for_context(session, "control", "你好，你的毕设八音盒已经上线。")
        boot_control["offline_music_id"] = 0
        boot_control["tts_enabled"] = True
        boot_control["mood_music_enabled"] = False
        boot_control["display_title"] = "在线欢迎"
        await send_reply_bundle(websocket, boot_control)

        async for message in websocket:
            if isinstance(message, bytes):
                if recording:
                    pcm_buffer.extend(message)
                    # 收到设备发来的 PCM 二进制分片，追加到缓冲区
                    print(f"[WS] received pcm chunk: {len(message)} bytes, total={len(pcm_buffer)}")
                else:
                    # 未处于录音状态却收到二进制帧，记录并忽略
                    print(f"[WS] unexpected binary frame: {len(message)} bytes")
                continue

            text = message.strip()
            # 文本消息通常为命令或查询
            print(f"[WS] received text: {text}")

            if text == "CMD:START_RECORD":
                recording = True
                pcm_buffer.clear()
                # 设备通知开始录音，开启缓冲收集
                print("[ASR] start receiving audio stream from ESP32")
                continue

            if text == "CMD:STOP_RECORD":
                recording = False
                # 设备通知结束录音，把缓冲的 PCM 发送给 ASR 处理
                await handle_voice_query(websocket, session, bytes(pcm_buffer))
                pcm_buffer.clear()
                continue

            if text.startswith("{"):
                try:
                    payload = json.loads(text)
                except json.JSONDecodeError as exc:
                    # 解析 JSON 失败，记录并回复离线兜底错误信息
                    print(f"[WS] invalid json payload: {exc}")
                    await send_reply_bundle(websocket, build_offline_control(session, f"JSON ????: {exc}"))
                    continue

                msg_type = payload.get("type")
                if msg_type == "heartbeat":
                    continue
                if msg_type == "scene_event":
                    await handle_scene_event(websocket, session, payload)
                    continue

            await handle_text_query(websocket, session, text)

    except websockets.exceptions.ConnectionClosed as exc:
        # 连接关闭，打印原因并结束会话处理
        print(f"[WS] device disconnected: {exc}")


async def main() -> None:
    """启动 WebSocket 服务并常驻运行。"""
    # 启动 WebSocket 服务，监听 8765 端口
    print("[Server] Python WebSocket server started.")
    print("[Server] Listening on 0.0.0.0:8765")
    async with websockets.serve(handle_client, "0.0.0.0", 8765, max_size=4 * 1024 * 1024):
        await asyncio.Future()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        # 用户中断服务器（Ctrl+C），优雅退出
        print("\n[Server] Server stopped by user.")
