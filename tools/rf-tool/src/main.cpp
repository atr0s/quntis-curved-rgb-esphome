// RF research tool for Quntis XN297 remotes.
// Wiring (ESP32-C3 Super Mini): CE=1 CSN=5 SCK=2 MISO=3 MOSI=4, VCC=3V3.
//
// Serial commands at 115200 baud, newline terminated:
//   z                      sweep channels 0-83 at 1M/2M/250k listening for the XN297 preamble;
//                          report channels where packets (and repeats) are received
//   q <ch>                 capture raw 32-byte frames on one channel at 1 Mbps
//   a <addr10hex> <id8hex> set the address and remote ID used by 't'
//                          (default AA415F98E7 833B6723)
//   t <cmd> <bright> <cct> <backlight> <mode> <ctr> [cycles]
//                          build and transmit a frame (hex values) on channels 19/53/80
//   x                      stop
#include <Arduino.h>
#include <RF24.h>
#include <SPI.h>

static const int CE_PIN = 1, CSN_PIN = 5, SCK_PIN = 2, MISO_PIN = 3, MOSI_PIN = 4;
static const uint8_t PREAMBLE[3] = {0x55, 0x0F, 0x71};
static const uint8_t SCRAMBLE[16] = {0xe3, 0xb1, 0x4b, 0xea, 0x85, 0xbc, 0xe5, 0x66,
                                     0x0d, 0xae, 0x8c, 0x88, 0x12, 0x69, 0xee, 0x1f};
static const int TX_CHANNELS[] = {19, 53, 80};

RF24 radio(CE_PIN, CSN_PIN);
enum Mode { IDLE, CAPTURE, SWEEP } mode = IDLE;
uint8_t address[5] = {0xAA, 0x41, 0x5F, 0x98, 0xE7};
uint8_t remote_id[4] = {0x83, 0x3B, 0x67, 0x23};
String line;

static const char *rate_name(rf24_datarate_e r) { return r == RF24_250KBPS ? "250k" : r == RF24_1MBPS ? "1M" : "2M"; }

static uint8_t bit_reverse(uint8_t b) {
    b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
    b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
    return (b & 0xAA) >> 1 | (b & 0x55) << 1;
}

static uint16_t crc16(const uint8_t *d, int n) {
    uint16_t c = 0xB5D2;
    for (int i = 0; i < n; i++) {
        c ^= d[i] << 8;
        for (int k = 0; k < 8; k++) c = (c & 0x8000) ? (c << 1) ^ 0x1021 : c << 1;
    }
    return c;
}

static void configure_rx(int ch, rf24_datarate_e rate, uint8_t payload_size) {
    radio.stopListening();
    radio.setAutoAck(false);
    radio.disableCRC();
    radio.disableDynamicPayloads();
    radio.setAddressWidth(3);
    radio.setPayloadSize(payload_size);
    radio.setRetries(0, 0);
    radio.setDataRate(rate);
    radio.setChannel(ch);
    radio.openReadingPipe(0, PREAMBLE);
    radio.startListening();
}

static void sweep() {
    static const rf24_datarate_e rates[] = {RF24_1MBPS, RF24_2MBPS, RF24_250KBPS};
    for (auto r : rates) {
        Serial.printf("[sweep] rate=%s\n", rate_name(r));
        for (int ch = 0; ch < 84; ch++) {
            configure_rx(ch, r, 16);
            uint8_t last[16] = {0};
            int n = 0, repeats = 0;
            unsigned long t = millis();
            while (millis() - t < 800) {
                while (radio.available()) {
                    uint8_t buf[16];
                    radio.read(buf, 16);
                    if (n && memcmp(buf, last, 13) == 0) repeats++;
                    memcpy(last, buf, 16);
                    n++;
                }
                if (Serial.available()) return;
            }
            if (n) Serial.printf("[sweep] ch=%d rate=%s packets=%d repeats=%d\n", ch, rate_name(r), n, repeats);
        }
    }
}

static void transmit(const uint8_t *p10, int cycles) {
    uint8_t p[11], raw[18];
    p[10] = 0;
    for (int i = 0; i < 10; i++) { p[i] = p10[i]; p[10] += p10[i]; }
    for (int i = 0; i < 5; i++) raw[i] = address[4 - i] ^ SCRAMBLE[i];
    for (int i = 0; i < 11; i++) raw[5 + i] = bit_reverse(p[i]) ^ SCRAMBLE[5 + i];
    uint16_t c = crc16(raw, 16) ^ 0xF494;
    raw[16] = c >> 8;
    raw[17] = c & 0xFF;

    radio.stopListening();
    radio.setAutoAck(false);
    radio.disableCRC();
    radio.disableDynamicPayloads();
    radio.setAddressWidth(3);
    radio.setPayloadSize(18);
    radio.setDataRate(RF24_1MBPS);
    radio.setRetries(0, 0);
    radio.setPALevel(RF24_PA_LOW);
    radio.openWritingPipe(PREAMBLE);
    for (int k = 0; k < cycles; k++)
        for (int ch : TX_CHANNELS) {
            radio.setChannel(ch);
            radio.write(raw, 18);
        }
    Serial.print("[tx] ");
    for (int i = 0; i < 18; i++) Serial.printf("%02X", raw[i]);
    Serial.printf(" x%d\n", cycles);
    mode = IDLE;
}

static void parse_hex(const char *s, uint8_t *out, int n) {
    for (int i = 0; i < n && s[2 * i] && s[2 * i + 1]; i++) {
        char b[3] = {s[2 * i], s[2 * i + 1], 0};
        out[i] = strtoul(b, nullptr, 16);
    }
}

static void handle(const String &cmd) {
    if (cmd == "z") {
        mode = SWEEP;
    } else if (cmd.startsWith("q ")) {
        int ch = cmd.substring(2).toInt();
        configure_rx(ch, RF24_1MBPS, 32);
        mode = CAPTURE;
        Serial.printf("[q] ch=%d\n", ch);
    } else if (cmd.startsWith("a ")) {
        char a[11] = {0}, id[9] = {0};
        sscanf(cmd.c_str() + 2, "%10s %8s", a, id);
        parse_hex(a, address, 5);
        parse_hex(id, remote_id, 4);
        Serial.printf("[a] address=%02X%02X%02X%02X%02X id=%02X%02X%02X%02X\n", address[0], address[1], address[2],
                      address[3], address[4], remote_id[0], remote_id[1], remote_id[2], remote_id[3]);
    } else if (cmd.startsWith("t ")) {
        unsigned v[6] = {0};
        int cycles = 40;
        sscanf(cmd.c_str() + 2, "%x %x %x %x %x %x %d", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &cycles);
        uint8_t p10[10] = {remote_id[0], remote_id[1], remote_id[2], remote_id[3], (uint8_t)v[0],
                           (uint8_t)v[1], (uint8_t)v[2], (uint8_t)v[3], (uint8_t)v[4], (uint8_t)v[5]};
        transmit(p10, cycles);
    } else if (cmd == "x") {
        radio.stopListening();
        mode = IDLE;
        Serial.println("[idle]");
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, CSN_PIN);
    if (!radio.begin(&SPI, CE_PIN, CSN_PIN)) {
        Serial.println("[ERROR] NRF24L01 not found, check wiring");
        while (1) delay(1000);
    }
    Serial.println("[ready] commands: z | q <ch> | a <addr> <id> | t <cmd> <bright> <cct> <bl> <mode> <ctr> | x");
}

void loop() {
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            if (line.length()) handle(line);
            line = "";
        } else {
            line += c;
        }
    }
    if (mode == SWEEP) {
        sweep();
        if (!Serial.available()) mode = IDLE;
    } else if (mode == CAPTURE) {
        while (radio.available()) {
            uint8_t buf[32];
            radio.read(buf, 32);
            Serial.printf("%lu ", millis());
            for (int i = 0; i < 32; i++) Serial.printf("%02X", buf[i]);
            Serial.println();
        }
    }
}
