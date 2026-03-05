# Overview

Add context, skills and commands that make doing EC development with agents more efficient.

# Guidelines

Check for sensitive or private data before submitting! Internal only context, skills or commands should be added to the internal cros-ec-development extension instead.

# Context

Context imports via @ are not allowed to traverse to parent directories. This means importing context
from ../docs to a context file in this .agents directory will not work.

Importing docs for context is often not a good idea anyways, since it can bloat the context. Instead copy and paste concise context from ../docs as needed or create a symlink in .agents/context/ (see ec_terms.md for an example). The agent can help cros reference the context against the docs to find inconsistencies.
