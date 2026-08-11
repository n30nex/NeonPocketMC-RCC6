# Ultimate RCC6 capture provenance

These images were captured from the connected Heltec RadioCore RCC6 qualification unit through a diagnostic-only USB framebuffer export. They are actual 220×128 frames produced by the production Ultimate renderer.

- production UI source: `7b7cd65f04dfe75e1535c17ba4fc9b8c2b807617`
- capture harness source: `82653f5ce83b9ca7c2117c4ab4a8e1d1d746e8d7`
- wire format: 220×128 little-endian RGB565 with a CRC32-checked header and trailer
- published scaling: 4× nearest-neighbor; no smoothing, recoloring, or compositing
- demo content: sanitized local messages, radio names, and RF statistics created only for capture

The capture harness is not part of either production firmware image. After capture, its isolated `/np/` demo history was cleared and the qualification unit was restored to a production Ultimate BLE application image.
