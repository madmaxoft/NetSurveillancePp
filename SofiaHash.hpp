#pragma once

#include <string>





namespace NetSurveillancePp
{





/** Calculates the Sofia-variant of MD5 on the input string.
Basically regular MD5, with the digest digits pairwise summed and mapped onto a specific output character set. */
std::string sofiaHash(const std::string & aInput);

}  // namespace NetSurveillancePp
