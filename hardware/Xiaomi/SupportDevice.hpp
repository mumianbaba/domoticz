#pragma once


#include <iostream>
#include <map>
#include "Outlet.hpp"
#include "DevAttr.hpp"
#include "Device.hpp"
#include "OutletChild.cpp"

namespace XiaoMi{


static const OnOffOutlet onoff0{ 1, 3, {{"channel_0", "on", true}, {"channel_0", "off", false}}};
static const OnOffOutlet onoff1{ 2, 3, {{"channel_1", "on", true}, {"channel_1", "off", false}}};
static const OnOffOutlet onoff2{ 3, 3, {{"channel_2", "on", true}, {"channel_2", "off", false}}};
static const OnOffOutlet onoff3{ 4, 3, {{"channel_3", "on", true}, {"channel_3", "off", false}}};


static const DevInfo  devInfoTab[] {
	{
		name : "Aqara Wall Dual Switch",
		zigbeeModel : "lumi.ctrl_neutral2",
		model : "QBKG03LM",
		vendor : "lumi",
		outlet : {&onoff0, &onoff1}
	},
	{
		name : "Aqara Wall Single Switch",
		zigbeeModel : "lumi.ctrl_neutral1",
		model : "QBKG03LM",
		vendor : "lumi",
		outlet : {&onoff0}
	}
};

}

