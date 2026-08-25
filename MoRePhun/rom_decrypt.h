#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "vmgp_header.h"

bool decryptCommercialCode(std::vector<uint8_t>& rom, const VMGPHeader& header, std::string& error);
