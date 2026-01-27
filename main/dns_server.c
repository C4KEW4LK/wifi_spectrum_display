#include "dns_server.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"
#include "esp_log.h"
#include "esp_netif.h"

#define DNS_PORT 53
#define TAG "DNS_SRV"

static int sock_fd = -1;
static TaskHandle_t dns_task_handle = NULL;
static bool dns_running = false;

// DNS Header structure
typedef struct __attribute__((packed)) {
    uint16_t id;
    uint16_t flags;
    uint16_t qd_count;
    uint16_t an_count;
    uint16_t ns_count;
    uint16_t ar_count;
} dns_header_t;

static void dns_server_task(void *pvParameters) {
    char rx_buffer[128];
    char tx_buffer[128];
    struct sockaddr_in source_addr;
    socklen_t socklen = sizeof(source_addr);

    while (dns_running) {
        int len = recvfrom(sock_fd, rx_buffer, sizeof(rx_buffer), 0, (struct sockaddr *)&source_addr, &socklen);
        if (len < 0) {
            ESP_LOGE(TAG, "recvfrom failed: errno %d", errno);
            break;
        }

        if (len > sizeof(dns_header_t)) {
            dns_header_t *header = (dns_header_t *)rx_buffer;
            
            // Prepare response (Echo ID, Set generic flags for response)
            // Flags: QR=1 (Response), Opcode=0 (Standard), AA=1 (Authoritative), RA=0, RCODE=0
            // 0x8400 approx.
            
            memcpy(tx_buffer, rx_buffer, len);
            dns_header_t *resp_header = (dns_header_t *)tx_buffer;
            resp_header->flags = htons(0x8400); // Standard response, no error
            resp_header->an_count = htons(1);   // 1 Answer

            // Pointer to query section (skip header)
            char *query = tx_buffer + sizeof(dns_header_t);
            char *end_of_query = query;
            
            // Skip QNAME
            while (*end_of_query != 0) {
                end_of_query += (*end_of_query) + 1;
                if (end_of_query - tx_buffer >= len) break;
            }
            end_of_query++; // Skip null byte
            end_of_query += 4; // Skip QTYPE and QCLASS

            int query_len = end_of_query - tx_buffer;
            if (query_len > sizeof(tx_buffer) - 16) continue; // Safety check

            // Append Answer
            // Name: Ptr to QNAME (0xC00C)
            tx_buffer[query_len++] = 0xC0;
            tx_buffer[query_len++] = 0x0C;
            
            // TYPE: A (0x0001)
            tx_buffer[query_len++] = 0x00;
            tx_buffer[query_len++] = 0x01;
            
            // CLASS: IN (0x0001)
            tx_buffer[query_len++] = 0x00;
            tx_buffer[query_len++] = 0x01;
            
            // TTL: 60s
            tx_buffer[query_len++] = 0x00;
            tx_buffer[query_len++] = 0x00;
            tx_buffer[query_len++] = 0x00;
            tx_buffer[query_len++] = 0x3C;
            
            // RDLENGTH: 4 bytes
            tx_buffer[query_len++] = 0x00;
            tx_buffer[query_len++] = 0x04;
            
            // RDATA: IP Address (192.168.4.1)
            tx_buffer[query_len++] = 192;
            tx_buffer[query_len++] = 168;
            tx_buffer[query_len++] = 4;
            tx_buffer[query_len++] = 1;

            sendto(sock_fd, tx_buffer, query_len, 0, (struct sockaddr *)&source_addr, sizeof(source_addr));
        }
    }
    vTaskDelete(NULL);
}

void dns_server_start(void) {
    if (dns_running) return;

    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(DNS_PORT);

    sock_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock_fd < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        return;
    }

    int err = bind(sock_fd, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err < 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        close(sock_fd);
        return;
    }

    dns_running = true;
    xTaskCreate(dns_server_task, "dns_server", 4096, NULL, 5, &dns_task_handle);
    ESP_LOGI(TAG, "DNS Server started");
}

void dns_server_stop(void) {
    if (!dns_running) return;
    dns_running = false;
    shutdown(sock_fd, 0);
    close(sock_fd);
    sock_fd = -1;
    // Task will delete itself
    ESP_LOGI(TAG, "DNS Server stopped");
}
