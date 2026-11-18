#pragma once

// Nonblocking USB serial command input for the diagnostic firmware.
// Returns true when a new Wi-Fi connection has been requested.
bool networkTestPoll();
bool networkTestRunning();
