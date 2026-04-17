# Pigweed Tokenized Logging for the EC

This snippet enables tokenized logging for the EC application.  For more
details, refer to the [Zephyr EC Tokenized Logging] documentation.

[Zephyr EC Tokenized Logging]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/ec/docs/zephyr/zephyr_tokenized_logging.md

To use this snippet, add or append the `snippets` parameter in your project's
BUILD.py file.

Example:
```python
register_trulo_project(
    project_name="pujjocento",
    chip="npcx9/npcx9m7fb",
    kconfig_files=[
        # Common to all projects.
        here / "program.conf",
        # Parent project's config
        here / "pujjocento" / "project.conf",
    ],
    snippets=["pw-tokenize"],
)
```
