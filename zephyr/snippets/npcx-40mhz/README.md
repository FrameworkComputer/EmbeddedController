# NPCX EC 40 MHz

This snippet configures the NPCX EC chipset for 40MHz operation,
improving performance.

To use this snippet, add or append the `snippets` parameter in your project's
BUILD.py file.

Example:
```python
pujjoga = register_nissa_project(
    project_name="pujjoga",
    chip="npcx9/npcx9m3f",
    snippets=["npcx-40mhz"],
)
```
