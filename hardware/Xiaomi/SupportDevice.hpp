#pragma once


#include <iostream>
#include <map>
#include "Outlet.hpp"
#include "DevAttr.hpp"
#include "Device.hpp"
#include "OutletChild.cpp"

namespace XiaoMi{

static const TbGateway tenbayGW {};


static const OnOffOutlet onoff0{ 1, 3, {{"channel_0", "on", true}, {"channel_0", "off", false}}};
static const OnOffOutlet onoff1{ 2, 3, {{"channel_1", "on", true}, {"channel_1", "off", false}}};
static const OnOffOutlet onoff2{ 3, 3, {{"channel_2", "on", true}, {"channel_2", "off", false}}};
static const OnOffOutlet onoff3{ 4, 3, {{"channel_3", "on", true}, {"channel_3", "off", false}}};

/*
static const KwhOutlet  kwh{ unit, dir, {{"key1", "valuetype", k}, {"key2", "valuetype", k}}};

*/
static const KwhOutlet  kwh{ 1, 3, {{"load_power", "float", 1}, {"energy_consumed", "float", 1}}};


static const LuxOutlet  lux{ 1, 3, {{"illumination", "int", 1}, {"lux", "int", 1}}};



/*
	binary value sensor, such as water leak, smoke detect,  move motion.

static const SensorBinOutlet SensorXxxxx
	{ 
		 unit, swType , reserve, 
		{
			{"key", "value1", true},
			{"key", "value2", false},
			{"key", "value3", false}
		}
	};
*/

static const SensorBinOutlet SensorContact
	{ 
		1, 2, 3, 
		{
			{"window_status", "open", true},
			{"window_status", "close", false},
			{"window_status", "unknown", false}
		}
	};

static const SensorBinOutlet SensorWLeak
	{ 
		1, 5, 3, 
		{
			{"wleak_status", "leak", true},
			{"wleak_status", "normal", false},
		}
	};
static const SensorBinOutlet SensorGas
	{ 
		1, 5, 3, 
		{
			{"gas_status", "yes", true},
			{"gas_status", "no", false},
		}
	};

static const SensorBinOutlet SensorSmoke
	{ 
		1, 5, 3, 
		{
			{"smoke_status", "yes", true},
			{"smoke_status", "no", false},
		}
	};


static const SensorBinOutlet SensorMotion
	{ 
		1, 8, 3, 
		{
			{"motion_status", "motion", true},
			{"motion_status", "unknown", false}
		}
	};

static const SelectorOutlet SelectorVib
	{
		1, 3, "SelectorStyle:0;LevelNames:Off|vibration|tilt|drop",
		{
			{"status", "touch", 10},
			{"status", "tilt", 20},
			{"status", "drop", 30}
		}
	};

static const SelectorOutlet SelectorWirlessOne
	{
		1, 3, "SelectorStyle:0;LevelNames:Off|Click|Double Click|Long Click",
		{
			{"button_0", "click", 10},
			{"button_0", "double_click", 20},
			{"button_0", "long_click", 30}
		}
	};

static const SelectorOutlet SelectorWirlessOnePlus
	{
		1, 3, "SelectorStyle:0;LevelNames:Off|Click|Double Click|Long Click|Shake",
		{
			{"button_0", "click", 10},
			{"button_0", "double_click", 20},
			{"button_0", "long_click", 30},
			{"button_0", "shake", 40}
		}
	};

static const SelectorOutlet SelectorWirlessTow
	{
		1, 3, "SelectorStyle:0;LevelNames:Off|S1|S1 Double Click|S1 Long Click|S2|S2 Double Click|S2 Long Click|Both Click|Both Double Click|Both Long Click",
		{
			{"button_0", "click", 10},
			{"button_0", "double_click", 20},
			{"button_0", "long_click", 30},
			{"button_1", "click", 40},
			{"button_1", "double_click", 50},
			{"button_1", "long_click", 60},
			{"dual_channel", "click", 70},
			{"dual_channel", "double_click", 80},
			{"dual_channel", "long_click", 90}
		}
	};


static const SelectorOutlet SelectorCube
	{
		1, 3, "SelectorStyle:0;LevelNames:Off|flip90|flip180|move|tap_twice|shake|swing|alert|free_fall|rotate",
		{
			{"cube_status", "flip90", 10},
			{"cube_status", "flip180", 20},
			{"cube_status", "move", 30},
			{"cube_status", "tap_twice", 40},
			{"cube_status", "shake", 50},
			{"cube_status", "swing", 00},
			{"cube_status", "alert", 00},
			{"cube_status", "free_fall", 00},
			{"cube_status", "rotate", 90}
		}
	};




static const WeatherOutlet weatherTHB
	{
		1, 3, WeatherType::WeatherTHBaro ,
		{
			{"temperature", "float", 1},
			{"humidity", "float", 1},
			{"pressure", "float", 1}
		}
	};

static const WeatherOutlet weatherTH
	{
		1, 3, WeatherType::WeatherTHum ,
		{
			{"temperature", "float", 1},
			{"humidity", "float", 1}
		}
	};

	
static const LedOutlet ledTemp
	{
		1, 3, LedType::LedTemp ,
		{
			{"power_status", "string", 1},
			{"light_level", "int", 1},
			{"color_temp", "int", 1}
		}
	};






static const DevInfo  devInfoTab[] {
	/**/
	{
		name : "SM-4Z",
		zigbeeModel : "TBL-V01-GL",
		model : "xxxxx",
		vendor : "tenbay",
		outlet : {&tenbayGW},
		timeout : 1800
	},
	/* onoff */
	{
		name : "Aqara Wall Dual Switch",
		zigbeeModel : "lumi.ctrl_neutral2",
		model : "QBKG03LM",
		vendor : "lumi",
		outlet : {&onoff0, &onoff1},
		timeout : 1800
	},
	{
		name : "Aqara Wall Single Switch",
		zigbeeModel : "lumi.ctrl_neutral1",
		model : "QBKG03LM",
		vendor : "lumi",
		outlet : {&onoff0},
		timeout : 1800
	},
	{
		name : "Tuya Wall Dual Switch",
		zigbeeModel : "TS0012",
		model : "TY-ZL-UN-LB2-W",
		vendor : "tuya",
		outlet : {&onoff0, &onoff1},
		timeout : 1800
	},
	{
		name : "Tuya Wall Single Switch",
		zigbeeModel : "TS0011",
		model : "TY-ZL-UN-LB1-W",
		vendor : "tuya",
		outlet : {&onoff0},
		timeout : 1800
	},
	/* door sensor */
	{
		name : "Aqara Door Sensor",
		zigbeeModel : "lumi.sensor_magnet.aq2",
		model : "MCCGQ11LM",
		vendor : "lumi",
		outlet : {&SensorContact},
		timeout : 7200
	},
	{
		name : "Feibit Door Sensor",
		//zigbeeModel : "FNB54-DOS09ML0.7",
		zigbeeModel : "FNB54-DOS09ML",
		model : "NDOS109W-N1",
		vendor : "feibit",
		outlet : {&SensorContact},
		timeout : 7200
	},
	/* wleak sensor */
	{
		name : "Aqara Water Sensor",
		zigbeeModel : "lumi.sensor_wleak.aq1",
		model : "SJCGQ11LM",
		vendor : "lumi",
		outlet : {&SensorWLeak},
		timeout : 7200
	},
	{
		name : "Feibit Water Sensor",
		//zigbeeModel : "FNB54-WTS08ML1.0",
		zigbeeModel : "FNB54-WTS08ML",
		model : "NWTS108W-N1",
		vendor : "feibit",
		outlet : {&SensorWLeak},
		timeout : 7200
	},
	/* gas sensor */
	{
		name : "Feibit Gas Sensor",
		//zigbeeModel : "FNB54-GAS07ML0.8",
		zigbeeModel : "FNB54-GAS07ML",
		model : "NGAS107W-N1",
		vendor : "feibit",
		outlet : {&SensorGas},
		timeout : 7200
	},
	/* smoke sensor */
	{
		name : "Feibit Smoke Sensor",
		//zigbeeModel : "FNB54-SMF0AML0.9",
		zigbeeModel : "FNB54-SMF0AML",
		model : "NSMF10AW-N1",
		vendor : "feibit",
		outlet : {&SensorSmoke},
		timeout : 7200
	},
	/* motion sensor */
	{
		name : "Aqara Motion Sensor",
		zigbeeModel : "lumi.sensor_motion.aq2",
		model : "RTCGQ11LM",
		vendor : "lumi",
		outlet : {&SensorMotion, &lux},
		timeout : 7200
	},
	{
		name : "Tuya Motion Sensor",
		zigbeeModel : "RH3040",
		model : "TP001-ZA",
		vendor : "tuya",
		outlet : {&SensorMotion},
		timeout : 7200
	},
	{
		name : "Feibit Motion Sensor",
		//zigbeeModel : "FNB54-BOT0AML0.9",
		zigbeeModel : "FNB54-BOT0AML",
		model : "NTHM217W-N1",
		vendor : "feibit",
		outlet : {&SensorMotion},
		timeout : 7200
	},
	/* plug */
	{
		name : "Aqara Smart Plug",
		zigbeeModel : "lumi.plug.maeu01",
		model : "SP-EUC01",
		vendor : "lumi",
		outlet : {&onoff0, &kwh},
		timeout : 1800
	},
	/* selector */
	{
		name : "Aqara Vibration Sensor",
		zigbeeModel : "lumi.vibration.aq1",
		model : "DJT11LM",
		vendor : "lumi",
		outlet : {&SelectorVib},
		timeout : 7200
	},
	{
		name : "Aqara Wireless Single Switch",
		zigbeeModel : "lumi.remote.b186acn01",
		model : "WXKG03LM",
		vendor : "lumi",
		outlet : {&SelectorWirlessOne},
		timeout : 7200
	},
	{
		name : "Aqara Wireless Dual Switch",
		zigbeeModel : "lumi.remote.b286acn01",
		model : "WXKG02LM",
		vendor : "lumi",
		outlet : {&SelectorWirlessTow},
		timeout : 7200
	},
	{
		name : "Aqara Mini Switch Plus",
		zigbeeModel : "lumi.sensor_switch.aq3",
		model : "WXKG12LM",
		vendor : "lumi",
		outlet : {&SelectorWirlessOnePlus},
		timeout : 7200
	},
	{
		name : "Aqara Cube",
		zigbeeModel : "lumi.sensor_cube.aqgl01",
		model : "MFKZQ01LM",
		vendor : "lumi",
		outlet : {&SelectorCube},
		timeout : 7200
	},
	{
		name : "Aqara Mini Switch",
		zigbeeModel : "lumi.remote.b1acn01",
		model : "WXKG11LM",
		vendor : "lumi",
		outlet : {&SelectorWirlessOne},
		timeout : 7200
	},
	{
		name : "Tuya Wireless Single Switch",
		zigbeeModel : "TS0041",
		model : "DJT11LM",
		vendor : "tuya",
		outlet : {&SelectorWirlessOne},
		timeout : 7200
	},
	/* weather */
	{
		name : "Aqara Temp Sensor",
		zigbeeModel : "lumi.weather",
		model : "WSDCGQ11LM",
		vendor : "lumi",
		outlet : {&weatherTHB},
		timeout : 7200
	},
	{
		name : "Tuya Temp Sensor",
		zigbeeModel : "RH3052",
		model : "TT081",
		vendor : "tuya",
		outlet : {&weatherTH},
		timeout : 7200
	},
	{
		name : "Feibit Temp Sensor",
		//zigbeeModel : "FNB54-THM17ML1.1",
		zigbeeModel : "FNB54-THM17ML",
		model : "NTHM217W-N1",
		vendor : "feibit",
		outlet : {&weatherTH},
		timeout : 7200
	},
	{
		name : "Aqara LED Bulb",
		zigbeeModel : "lumi.light.aqcn02",
		model : "ZNLDP12LM",
		vendor : "lumi",
		outlet : {&ledTemp},
		timeout : 1800
	}
};

}

