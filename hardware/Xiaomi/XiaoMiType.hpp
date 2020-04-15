#pragma once
#include <iostream>
#include <vector>
#include <string>
#include <boost/tuple/tuple.hpp>
#include <initializer_list>
#include <list>


#include "../hardwaretypes.h"
#include "../../main/RFXNames.h"



typedef std::pair<unsigned int, std::string>   SsidPair;
typedef boost::tuple<std::string  /* key */, std::string /* value */, bool> Rule_OnOff;




//#define pTypeGeneralSwitch			0xF4
//#define sSwitchGeneralSwitch		0x49
//#define STYPE_OnOff  				0


