#include <cassert>
#include <string>
#include "../src/network_test.cpp"

int main() {
    Serial.feed("WIFI\tTestNetwork\tprivate-password\n");
    assert(networkTestPoll());
    assert(WiFi.ssid == "TestNetwork" && WiFi.password == "private-password");
    assert(!WiFi.persistence);
    assert(Serial.output.find("private-password") == std::string::npos);
    Serial.feed("LOAD\t127.0.0.1\t9876\n");
    assert(!networkTestPoll() && networkTestRunning() && fakeTask);
    fakeTask(nullptr);
    assert(!networkTestRunning() && fakeBytes > 0 && fakeMillis == 30000);
    Serial.feed("LOAD\t127.0.0.1\t99999\n");
    networkTestPoll();
    assert(!networkTestRunning());
    Serial.feed("LOAD\t127.0.0.1\t9876\nSTOP\n");
    networkTestPoll();
    assert(networkTestRunning());
    uint32_t before = fakeBytes;
    fakeTask(nullptr);
    assert(!networkTestRunning() && fakeBytes == before);
    Serial.feed("WIFI\tbad\n");
    assert(!networkTestPoll());
    Serial.feed(std::string(220, 'x') + "\n");
    assert(!networkTestPoll());
    assert(Serial.output.find("Command too long") != std::string::npos);
}
