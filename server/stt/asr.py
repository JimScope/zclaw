"""NVIDIA Riva speech recognition hosted on NVCF, for the STT service."""

from __future__ import annotations

# parakeet-1.1b-rnnt-multilingual-asr on build.nvidia.com (confirmed by probe_asr.py)
PARAKEET_MULTILINGUAL = "71203149-d3b7-4460-8231-1be2543a1fca"
SAMPLE_RATE = 16000
CHUNK_BYTES = 3200  # 100 ms of PCM16 for streaming mode


def _join(results) -> str:
    return " ".join(r.alternatives[0].transcript.strip() for r in results if r.alternatives)


class RivaASR:
    """Callable: PCM16LE mono 16 kHz bytes -> transcript."""

    def __init__(self, api_key: str, function_id: str, language: str = "es-US",
                 server: str = "grpc.nvcf.nvidia.com:443", mode: str = "offline"):
        if mode not in ("offline", "streaming"):
            raise ValueError(f"unknown RIVA_MODE {mode!r}")
        import riva.client  # only the deployed service needs nvidia-riva-client

        self._riva = riva.client
        auth = riva.client.Auth(
            uri=server,
            use_ssl=True,
            metadata_args=[["function-id", function_id], ["authorization", f"Bearer {api_key}"]],
        )
        self._asr = riva.client.ASRService(auth)
        self._config = riva.client.RecognitionConfig(
            encoding=riva.client.AudioEncoding.LINEAR_PCM,
            sample_rate_hertz=SAMPLE_RATE,
            audio_channel_count=1,
            language_code=language,
            max_alternatives=1,
            enable_automatic_punctuation=True,
        )
        self._mode = mode

    def __call__(self, pcm: bytes) -> str:
        if self._mode == "streaming":
            return self._streaming(pcm)
        return _join(self._asr.offline_recognize(pcm, self._config).results)

    def _streaming(self, pcm: bytes) -> str:
        cfg = self._riva.StreamingRecognitionConfig(config=self._config, interim_results=False)
        chunks = [pcm[i:i + CHUNK_BYTES] for i in range(0, len(pcm), CHUNK_BYTES)]
        parts = []
        for response in self._asr.streaming_response_generator(audio_chunks=chunks, streaming_config=cfg):
            text = _join(r for r in response.results if getattr(r, "is_final", True))
            if text:
                parts.append(text)
        return " ".join(parts)
