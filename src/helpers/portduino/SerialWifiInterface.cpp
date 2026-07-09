#if defined(PORTDUINO)

#include "SerialWifiInterface.h"

void SerialWifiInterface::begin(int port) {
  server = new WiFiServer(port);
  server->begin();   // listens on all interfaces, non-blocking accept
}

void SerialWifiInterface::enable() {
  if (_isEnabled) return;
  _isEnabled = true;
  clearBuffers();
}

void SerialWifiInterface::disable() {
  _isEnabled = false;
}

size_t SerialWifiInterface::writeFrame(const uint8_t src[], size_t len) {
  if (len > MAX_FRAME_SIZE) {
    WIFI_DEBUG_PRINTLN("writeFrame(), frame too big, len=%d", (int) len);
    return 0;
  }

  if (deviceConnected && len > 0) {
    if (send_queue_len >= FRAME_QUEUE_SIZE) {
      WIFI_DEBUG_PRINTLN("writeFrame(), send_queue is full!");
      return 0;
    }

    send_queue[send_queue_len].len = len;
    memcpy(send_queue[send_queue_len].buf, src, len);
    send_queue_len++;
    return len;
  }
  return 0;
}

size_t SerialWifiInterface::checkRecvFrame(uint8_t dest[]) {
  if (server == NULL) return 0;

  auto newClient = server->available();
  if (newClient) {   // new connection replaces the active one
    deviceConnected = false;
    client.stop();
    client = newClient;
    rx_len = 0;
  }

  if (client.connected()) {
    if (!deviceConnected) {
      WIFI_DEBUG_PRINTLN("Got connection");
      deviceConnected = true;
    }
  } else {
    if (deviceConnected) {
      deviceConnected = false;
      WIFI_DEBUG_PRINTLN("Disconnected");
    }
    return 0;
  }

  if (send_queue_len > 0) {   // first, flush send queue
    int len = send_queue[0].len;

    uint8_t pkt[3 + MAX_FRAME_SIZE];  // same framing as the serial interface
    pkt[0] = '>';
    pkt[1] = (len & 0xFF);  // LSB
    pkt[2] = (len >> 8);    // MSB
    memcpy(&pkt[3], send_queue[0].buf, len);
    client.write(pkt, 3 + len);

    send_queue_len--;
    for (int i = 0; i < send_queue_len; i++) {
      send_queue[i] = send_queue[i + 1];
    }
    return 0;
  }

  // accumulate incoming bytes; available()/read() go byte-by-byte, which also
  // keeps the shim's connected() from tripping over EAGAIN on empty reads
  while (true) {
    size_t needed;
    if (rx_len < 3) {
      needed = 3;
    } else {
      size_t frame_len = rx_buf[1] | (rx_buf[2] << 8);
      if (rx_buf[0] != '<' || frame_len > MAX_FRAME_SIZE) {
        // bad or unexpected header: shift by one byte to resync
        WIFI_DEBUG_PRINTLN("bad frame header, resyncing");
        memmove(rx_buf, rx_buf + 1, --rx_len);
        continue;
      }
      needed = 3 + frame_len;
      if (rx_len == needed) {   // complete frame received
        memcpy(dest, rx_buf + 3, frame_len);
        rx_len = 0;
        return frame_len;
      }
    }

    if (client.available() <= 0) return 0;   // no more bytes for now
    int c = client.read();
    if (c < 0) return 0;
    rx_buf[rx_len++] = (uint8_t) c;
  }
}

#endif
