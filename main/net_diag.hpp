// Network facts for the console's `diag` report: the joined access point and
// the station's addresses, including the DNS servers DHCP handed out.
#pragma once

#include "hg/json.hpp"

namespace nabu {

// Adds a `net` object to the report. Meant for hg::App::on_diag.
void add_net_diag(hg::json::Value& report);

}  // namespace nabu
