#include "net_diag.hpp"

#include <cstdio>
#include <string>
#include <string_view>

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"

namespace nabu {
namespace {

const char* TAG = "nabu.net";

// esp_netif's key for the default Wi-Fi station interface.
constexpr const char* kStationIfKey = "WIFI_STA_DEF";

// Room for a colon-separated IPv6 address, the longest text printed here.
constexpr size_t kAddressChars = 40;
constexpr size_t kMacBytes = 6;
constexpr size_t kMacChars = 18;

// The slots esp_netif keeps DNS servers in, in lookup order.
constexpr esp_netif_dns_type_t kDnsSlots[] = {ESP_NETIF_DNS_MAIN, ESP_NETIF_DNS_BACKUP, ESP_NETIF_DNS_FALLBACK};

// Fields of the `net` object.
constexpr std::string_view kNetField = "net";
constexpr std::string_view kJoinedField = "joined";
constexpr std::string_view kSsidField = "ssid";
constexpr std::string_view kRssiField = "rssi";
constexpr std::string_view kChannelField = "channel";
constexpr std::string_view kMacField = "mac";
constexpr std::string_view kIpField = "ip";
constexpr std::string_view kMaskField = "mask";
constexpr std::string_view kGatewayField = "gw";
constexpr std::string_view kDnsField = "dns";

std::string ipv4_text(const esp_ip4_addr_t& addr) {
  char text[kAddressChars];
  std::snprintf(text, sizeof(text), IPSTR, IP2STR(&addr));
  return text;
}

std::string ip_text(const esp_ip_addr_t& addr) {
  if (addr.type == ESP_IPADDR_TYPE_V4) return ipv4_text(addr.u_addr.ip4);
  char text[kAddressChars];
  std::snprintf(text, sizeof(text), IPV6STR, IPV62STR(addr.u_addr.ip6));
  return text;
}

bool is_unset(const esp_ip_addr_t& addr) {
  if (addr.type == ESP_IPADDR_TYPE_V4) return addr.u_addr.ip4.addr == 0;
  for (const uint32_t part : addr.u_addr.ip6.addr) {
    if (part != 0) return false;
  }
  return true;
}

// ESP_ERR_WIFI_NOT_CONNECT while not joined: the report then says so.
void add_access_point(hg::json::Value& net) {
  wifi_ap_record_t ap = {};
  const bool joined = esp_wifi_sta_get_ap_info(&ap) == ESP_OK;
  net.set(kJoinedField, joined);
  if (!joined) return;
  net.set(kSsidField, reinterpret_cast<const char*>(ap.ssid))
      .set(kRssiField, static_cast<int>(ap.rssi))
      .set(kChannelField, static_cast<int>(ap.primary));
}

void add_mac(hg::json::Value& net) {
  uint8_t mac[kMacBytes] = {};
  const esp_err_t err = esp_wifi_get_mac(WIFI_IF_STA, mac);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "no station MAC: %s", esp_err_to_name(err));
    return;
  }
  char text[kMacChars];
  std::snprintf(text, sizeof(text), MACSTR, MAC2STR(mac));
  net.set(kMacField, text);
}

void add_addresses(esp_netif_t* station, hg::json::Value& net) {
  esp_netif_ip_info_t info = {};
  const esp_err_t err = esp_netif_get_ip_info(station, &info);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "no station addresses: %s", esp_err_to_name(err));
    return;
  }
  if (info.ip.addr == 0) return;  // no DHCP lease yet
  net.set(kIpField, ipv4_text(info.ip)).set(kMaskField, ipv4_text(info.netmask)).set(kGatewayField, ipv4_text(info.gw));
}

void add_dns_servers(esp_netif_t* station, hg::json::Value& net) {
  hg::json::Value servers = hg::json::Value::array();
  for (const esp_netif_dns_type_t slot : kDnsSlots) {
    esp_netif_dns_info_t dns = {};
    const esp_err_t err = esp_netif_get_dns_info(station, slot, &dns);
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "no DNS server in slot %d: %s", static_cast<int>(slot), esp_err_to_name(err));
      continue;
    }
    if (!is_unset(dns.ip)) servers.push(ip_text(dns.ip));
  }
  net.set(kDnsField, std::move(servers));
}

}  // namespace

void add_net_diag(hg::json::Value& report) {
  hg::json::Value net = hg::json::Value::object();
  add_access_point(net);
  add_mac(net);
  esp_netif_t* station = esp_netif_get_handle_from_ifkey(kStationIfKey);
  if (station) {
    add_addresses(station, net);
    add_dns_servers(station, net);
  } else {
    ESP_LOGW(TAG, "no %s interface", kStationIfKey);
  }
  report.set(kNetField, std::move(net));
}

}  // namespace nabu
