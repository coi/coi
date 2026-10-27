#pragma once

// coi mcp: a Model Context Protocol server on stdin/stdout, so an AI assistant can check Coi
// code with this compiler, look up its APIs, read its guide, search the registry and run tests.
// Returns 0 when the client closes the connection.
int mcp_command();
