#include <iostream>
#include <string>
#include <algorithm>
#include <ctype.h>
#include <functional>
#include <boost/tuple/tuple.hpp>

#include "Outlet.hpp"
#include "../hardwaretypes.h"

#include "OutletChild.hpp"
#include "../hardwaretypes.h"
#include "../../main/RFXNames.h"
#include "../../main/json_helper.h"




namespace XiaoMi{


inline SsidPair idConvert_1(const std::string& mac)
{
	unsigned int rowId;
	char szTmp[64];

	rowId = OutletAttr::macToUint(mac);
	sprintf(szTmp, "%08X", rowId);
	std::string  strSsid = szTmp;
	return std::make_pair(rowId, strSsid);
}


inline SsidPair idConvert_2(const std::string& mac)
{
	unsigned int rowId;
	char szTmp[64];
	rowId = OutletAttr::macToUint(mac);
	rowId = 0xffff & rowId;
	sprintf(szTmp, "%d", rowId);
	std::string  strSsid = szTmp;
	return std::make_pair(rowId, strSsid);
}



OnOffOutlet::OnOffOutlet(int unit, int dir, std::initializer_list<RuleOnOff> list)
	:OutletAttr(pTypeGeneralSwitch, sSwitchGeneralSwitch, static_cast<int>(::STYPE_OnOff), static_cast<int>(unit), static_cast<int>(dir), "")
{
	std::cout<<"OnOffOutlet init list size:"<<list.size()<<std::endl;
	for (const auto &itt : list)
	{
		m_rule.emplace_back(itt);
	}
	std::cout<<"OnOffOutlet rule number:"<<list.size()<<std::endl;
}


bool OnOffOutlet::recvFrom(std::string& root, void * miGateway) const
{
	std::cout<<"OnOffOutlet recvFrom"<<std::endl;
	if (!miGateway)
	{
		return false;
	}
	Json::Value jsRoot;
	bool result;
	bool commit = false;
	
	ParseJSon(root, jsRoot);
	if (jsRoot.isMember("params") == false)
	{
		std::cout<<"params not isMember"<<std::endl;
		return false;
	}

	std::string mac = jsRoot["sid"].asString();
	std::string Model = jsRoot["model"].asString();
	std::string cmd = jsRoot["cmd"].asString();
	Json::Value params = jsRoot["params"];

	std::string key;
	std::string value;

	for (int ii = 0; ii < params.size() && commit == false; ii++)
	{
		for (const auto & itt : m_rule)
		{
			key = boost::get<0>(itt);
			value = boost::get<1>(itt);
			if (params[ii].isMember(key)
				&& params[ii][key].asString() == value)
			{
				result = boost::get<2>(itt);
				commit = true;
				break;
			}
		}
	}
	std::cout<<"commit: "<<commit<<"   result:"<<result<<std::endl;

	if (commit)
	{
		_eSwitchType switchType = STYPE_OnOff;		
		int unit = getUnit();
		bool bIsOn = result;
		int level = (bIsOn == true)? 10 :0;
		std::string loadPower = "";
		std::string powerConsumed = "";
		int battery = 255;
		std::string name = "";
		
		XiaomiGateway* gw = static_cast<XiaomiGateway*>(miGateway);
		gw->InsertUpdateSwitch(mac, name, bIsOn, switchType, unit, level, cmd, loadPower, powerConsumed, battery);

	}

	return true;
}

bool OnOffOutlet::writeTo(const unsigned char* packet, int len, std::string& mac, std::string& model,std::string& gwMac,  std::string& key, void * miGateway) const
{
	std::cout<<"OnOffOutlet writeTo"<<std::endl;
	if (!miGateway)
	{
		return false;
	}
	
	_tGeneralSwitch *xcmd = (_tGeneralSwitch*)packet;
	std::string control;
	int channel;
	
	control = (xcmd->cmnd == gswitch_sOn)? "on" : "off";
	channel = xcmd->unitcode -1;
	if (channel < 0)
	{
		std::cout<<"channel less then 0"<<std::endl;
		return false;
	}

	std::string chn = "channel_" + std::to_string(channel);
		
	Json::Value root;
	root["cmd"] = "write";
	root["sid"] = mac;
	root["model"] = model;
	root["key"] = "@gatewaykey";
	root["params"][0][chn] = control;

	std::string message = JSonToRawString(root);
	XiaomiGateway* gw = static_cast<XiaomiGateway*>(miGateway);
	gw->SendMessageToGateway(message);
	return true;
}




SensorBinOutlet::SensorBinOutlet(int unit, int swType, int dir, std::initializer_list<RuleOnOff> list)
	:OutletAttr(pTypeGeneralSwitch, sSwitchGeneralSwitch, static_cast<int>(swType), static_cast<int>(unit), static_cast<int>(dir), "")
{
	std::cout<<"SensorBinOutlet init list size:"<<list.size()<<std::endl;
	for (const auto &itt : list)
	{
		m_rule.emplace_back(itt);
	}
	std::cout<<"SensorBinOutlet rule number:"<<list.size()<<std::endl;
}


bool SensorBinOutlet::recvFrom(std::string& root, void * miGateway) const
{
	std::cout<<"SensorBinOutlet recvFrom"<<std::endl;
	if (!miGateway)
	{
		return false;
	}
	Json::Value jsRoot;
	bool result;
	bool commit = false;
	
	ParseJSon(root, jsRoot);
	if (jsRoot.isMember("params") == false)
	{
		std::cout<<"params not isMember"<<std::endl;
		return false;
	}

	std::string mac = jsRoot["sid"].asString();
	std::string Model = jsRoot["model"].asString();
	std::string cmd = jsRoot["cmd"].asString();
	Json::Value params = jsRoot["params"];

	std::string key;
	std::string value;

	for (int ii = 0; ii < params.size() && commit == false; ii++)
	{
		for (const auto & itt : m_rule)
		{
			key = boost::get<0>(itt);
			value = boost::get<1>(itt);
			if (params[ii].isMember(key)
				&& params[ii][key].asString() == value)
			{
				result = boost::get<2>(itt);
				commit = true;
				break;
			}
		}
	}
	std::cout<<"commit: "<<commit<<"result: "<<result<<std::endl;

	if (commit)
	{
		_eSwitchType switchType = STYPE_OnOff;		
		int unit = getUnit();
		bool bIsOn = result;
		int level = (bIsOn == true)? 10 :0;
		std::string loadPower = "";
		std::string powerConsumed = "";
		int battery = 255;
		std::string name = "";
		
		XiaomiGateway* gw = static_cast<XiaomiGateway*>(miGateway);
		gw->InsertUpdateSwitch(mac, name, bIsOn, switchType, unit, level, cmd, loadPower, powerConsumed, battery);

	}

	return true;
}

bool SensorBinOutlet::writeTo(const unsigned char* packet, int len, std::string& mac, std::string& model,std::string& gwMac,  std::string& key, void * miGateway) const
{
	std::cout<<"SensorBinOutlet writeTo"<<std::endl;	
	return true;
}







KwhOutlet::KwhOutlet(int unit,   int dir, std::initializer_list<RuleOnOff> list)
	:OutletAttr(pTypeGeneral, sTypeKwh, 0, static_cast<int>(unit), static_cast<int>(dir), "")
{
	std::cout<<"KwhOutlet init list size:"<<list.size()<<std::endl;
	for (const auto &itt : list)
	{
		m_rule.emplace_back(itt);
	}
	std::cout<<"KwhOutlet rule number:"<<list.size()<<std::endl;
}


bool KwhOutlet::recvFrom(std::string& root, void * miGateway) const
{
	std::cout<<"KwhOutlet recvFrom"<<std::endl;
	if (!miGateway)
	{
		return false;
	}
	Json::Value jsRoot;
	bool commit = false;
	
	ParseJSon(root, jsRoot);
	if (jsRoot.isMember("params") == false)
	{
		std::cout<<"params not isMember"<<std::endl;
		return false;
	}

	std::string mac = jsRoot["sid"].asString();
	std::string Model = jsRoot["model"].asString();
	std::string cmd = jsRoot["cmd"].asString();
	Json::Value params = jsRoot["params"];

	std::vector<boost::tuple<std::string, std::string>> result;
	std::string key;
	
	int ii = 0;

	for (const auto & itt : m_rule)
	{
		result.emplace_back(boost::make_tuple(boost::get<0>(itt), ""));
	}

	for (ii = 0; ii < params.size(); ii++)
	{
		for (auto & itt : result)
		{
			key = boost::get<0>(itt);
			std::cout<<"key: "<<key<<std::endl;

			if (params[ii].isMember(key))
			{
				 boost::get<1>(itt) = params[ii][key].asString();
				 commit = true;
			}
		}
	}
	std::cout<<"commit: "<<commit<<std::endl;
	if (commit)
	{
		std::string name = "";
		std::string loadPower;
		std::string consumed;
		ii = result.size();
		switch(ii)
		{
			case 2:
			{
				consumed = boost::get<1>(result[1]);
			}
			case 1:
			{
				loadPower = boost::get<1>(result[0]);
			}
			break;
			default:
				std::cout<<"rule is invalid, please set loadPower first and consumed second"<<std::endl;
			break;
		}
		std::cout<<"ii="<< ii <<"  "<<loadPower<<" --result 2--  "<<consumed<<std::endl;

		int battery = 255;
		XiaomiGateway* gw = static_cast<XiaomiGateway*>(miGateway);
		gw->InsertUpdateKwh(mac, name, loadPower, consumed, battery);
	}

	return true;
}

bool KwhOutlet::writeTo(const unsigned char* packet, int len, std::string& mac, std::string& model,std::string& gwMac,  std::string& key, void * miGateway) const
{
	std::cout<<"KwhOutlet writeTo"<<std::endl;	
	return true;
}



SelectorOutlet::SelectorOutlet(int unit, int dir, std::string opts, std::initializer_list<RuleSelector> list)
	:OutletAttr(pTypeGeneralSwitch, sSwitchTypeSelector,  static_cast<int>(STYPE_Selector), static_cast<int>(unit), static_cast<int>(dir), opts)
{
	std::cout<<"SelectorOutlet init list size:"<<list.size()<<std::endl;
	for (const auto &itt : list)
	{
		m_rule.emplace_back(itt);
	}
	std::cout<<"SelectorOutlet rule number:"<<list.size()<<std::endl;
}


bool SelectorOutlet::recvFrom(std::string& root, void * miGateway) const
{
	std::cout<<"SelectorOutlet recvFrom"<<std::endl;
		if (!miGateway)
	{
		return false;
	}
	Json::Value jsRoot;

	ParseJSon(root, jsRoot);
	if (jsRoot.isMember("params") == false)
	{
		std::cout<<"params not isMember"<<std::endl;
		return false;
	}

	std::string mac = jsRoot["sid"].asString();
	std::string Model = jsRoot["model"].asString();
	std::string cmd = jsRoot["cmd"].asString();
	Json::Value params = jsRoot["params"];

	std::string key;
	std::string value;
	int result = 0;
	bool commit = false;

	for (int ii = 0; ii < params.size() && commit == false; ii++)
	{
		for (const auto & itt : m_rule)
		{
			key = boost::get<0>(itt);
			value = boost::get<1>(itt);
			std::cout<<"key:"<<key<<std::endl;
			std::cout<<"value:"<<key<<std::endl;
			std::cout<<"level:"<<boost::get<2>(itt)<<std::endl;
			if (params[ii].isMember(key)
				&& params[ii][key].asString() == value)
			{
				result = boost::get<2>(itt);
				commit = true;
				break;
			}
		}
	}
	std::cout<<"commit:"<<commit<<"   result:"<<result<<std::endl;

	if (commit)
	{
		_eSwitchType switchType = STYPE_Selector;		
		int unit = getUnit();
		bool bIsOn = (result > 0)? true : false;
		int level = result;
		std::string loadPower = "";
		std::string powerConsumed = "";
		int battery = 255;
		std::string name = "";
		
		XiaomiGateway* gw = static_cast<XiaomiGateway*>(miGateway);
		gw->InsertUpdateSwitch(mac, name, bIsOn, switchType, unit, level, cmd, loadPower, powerConsumed, battery);
	}

	return true;
}

bool SelectorOutlet::writeTo(const unsigned char* packet, int len, std::string& mac, std::string& model,std::string& gwMac,  std::string& key, void * miGateway) const
{
	std::cout<<"SelectorOutlet writeTo"<<std::endl;	
	return true;
}




WeatherOutlet::WeatherOutlet(int unit, int dir, WeatherType type, std::initializer_list<RuleWeather> list)
	:OutletAttr(typeConvert(type), subTypeConvert(type), 0, static_cast<int>(unit), static_cast<int>(dir), "")
{
	std::cout<<"WeatherOutlet init list size:"<<list.size()<<std::endl;
	for (const auto &itt : list)
	{
		m_rule.emplace_back(itt);
	}
	m_type = type;
	std::cout<<"WeatherOutlet rule number:"<<list.size()<<std::endl;
}


int WeatherOutlet::typeConvert(WeatherType type)
{
	int pType = 0;
	switch(type)
	{
		case WeatherType::WeatherTemp:
			pType = pTypeTEMP;
		break;
		case WeatherType::WeatherHum:
			pType = sTypeHUM1;
		break;
		case WeatherType::WeatherTHum:
			pType = pTypeTEMP_HUM;
		break;
		case WeatherType::WeatherTHBaro:
			pType = pTypeTEMP_HUM_BARO;
		break;
	}
	return pType;

}

int WeatherOutlet::subTypeConvert(WeatherType type)
{
	int subType = 0;
	switch(type)
	{
		case WeatherType::WeatherTemp:
			subType = sTypeTEMP5;
		break;
		case WeatherType::WeatherHum:
			subType = sTypeHUM1;
		break;
		case WeatherType::WeatherTHum:
			subType = sTypeTH5;
		break;
		case WeatherType::WeatherTHBaro:
			subType = sTypeTHB1;
		break;

	}
	return subType;

}

/* ssid and strSsid different every type in DT */
SsidPair WeatherOutlet::idConverter(const std::string& mac) const
{
	return idConvert(mac);
}


SsidPair WeatherOutlet::idConvert(const std::string& mac)
{
	unsigned int rowId;
	char szTmp[64];

	rowId = macToUint(mac);
	rowId = 0xffff & rowId;
	sprintf(szTmp, "%d", rowId);
	std::string  strSsid = szTmp;
	return std::make_pair(rowId, strSsid);
}

bool WeatherOutlet::recvFrom(std::string& root, void * miGateway) const
{
	std::cout<<"WeatherOutlet recvFrom"<<std::endl;
	if (!miGateway)
	{
		return false;
	}
	Json::Value jsRoot;
	bool commit = false;
	
	ParseJSon(root, jsRoot);
	if (jsRoot.isMember("params") == false)
	{
		std::cout<<"params not isMember"<<std::endl;
		return false;
	}

	std::string mac = jsRoot["sid"].asString();
	std::string Model = jsRoot["model"].asString();
	std::string cmd = jsRoot["cmd"].asString();
	Json::Value params = jsRoot["params"];

	std::vector<boost::tuple<std::string, std::string>> result;
	std::string key;
	
	int ii = 0;

	for (const auto & itt : m_rule)
	{
		result.emplace_back(boost::make_tuple(boost::get<0>(itt), ""));
	}

	for (ii = 0; ii < params.size(); ii++)
	{
		for (auto & itt : result)
		{
			key = boost::get<0>(itt);
			std::cout<<"key: "<<key<<std::endl;

			if (params[ii].isMember(key))
			{
				 boost::get<1>(itt) = params[ii][key].asString();
				 commit = true;
			}
		}
	}

	if (commit == false)
	{
		std::cout<<"WeatherOutlet commit: false, message content error"<<std::endl;
		return false;
	}

	std::string name = "";
	std::string temp = "";
	std::string hum = "";
	std::string baro = "";
	int battery = 255;

	ii = result.size();
	XiaomiGateway* gw = static_cast<XiaomiGateway*>(miGateway);

	switch(m_type)
	{
		case WeatherType::WeatherTemp:
			if (ii > 0)
			{
				temp = boost::get<1>(result[0]);
				gw->InsertUpdateTemperature(mac, name, ::atof(temp.c_str()), battery);
				return true;
			}
		break;
		case WeatherType::WeatherHum:
			if (ii > 0)
			{
				hum = boost::get<1>(result[0]);
				gw->InsertUpdateHumidity(mac, name, ::atoi(hum.c_str()), battery);
				return true;
			}
		break;
		case WeatherType::WeatherTHum:
			if (ii > 1)
			{
				temp = boost::get<1>(result[0]);
				hum  = boost::get<1>(result[1]);
				std::cout<<"-------WeatherTHum-----------"<<temp<<"      "<<hum<<" "<<std::endl;
				gw->InsertUpdateTempHum(mac, name, temp, hum, battery);
				return true;
			}
		break;
		case WeatherType::WeatherTHBaro:
			if (ii > 2)
			{
				temp = boost::get<1>(result[0]);
				hum  = boost::get<1>(result[1]);
				baro = boost::get<1>(result[2]);
				std::cout<<"-------WeatherTHBaro--------"<<temp<<"      "<<hum<<" "<<baro<<std::endl;
				gw->InsertUpdateTempHumPressure(mac, name, temp, hum, baro, battery);
				return true;
			}
		break;
	}
	std::cout<<"handle failed: result size:"<< ii<<" , type:"<<std::endl;
	return false;
}

bool WeatherOutlet::writeTo(const unsigned char* packet, int len, std::string& mac, std::string& model,std::string& gwMac,  std::string& key, void * miGateway) const
{
	std::cout<<"WeatherOutlet writeTo"<<std::endl;	
	return true;
}



LedOutlet::LedOutlet(int unit, int dir, LedType type, std::initializer_list<RuleLed> list)
	:OutletAttr(typeConvert(type), subTypeConvert(type), static_cast<int>(STYPE_Dimmer), static_cast<int>(unit), static_cast<int>(dir), "")
{
	std::cout<<"LedOutlet init list size:"<<list.size()<<std::endl;
	for (const auto &itt : list)
	{
		m_rule.emplace_back(itt);
	}
	m_type = type;
	std::cout<<"LedOutlet rule number:"<<list.size()<<std::endl;
}


int LedOutlet::typeConvert(LedType type)
{
	int pType = 0;
	switch(type)
	{
		case LedType::LedTemp:
		case LedType::LedRGB:
		case LedType::LedRGBTemp:
			pType = pTypeColorSwitch;
		break;
	}
	return pType;

}

int LedOutlet::subTypeConvert(LedType type)
{
	int subType = 0;
	switch(type)
	{
		case LedType::LedTemp:
			subType = sTypeColor_CW_WW;
		break;
		case LedType::LedRGB:
			subType = sTypeColor_RGB;
		break;
		case LedType::LedRGBTemp:
			subType = sTypeColor_RGB_CW_WW;
		break;
	}
	return subType;

}

SsidPair LedOutlet::idConvert(const std::string& mac)
{
	return idConvert_1(mac);
}

bool LedOutlet::recvFrom(std::string& root, void * miGateway) const
{
	std::cout<<"LedOutlet recvFrom"<<std::endl;
	if (!miGateway)
	{
		return false;
	}
	Json::Value jsRoot;
	bool commit = false;
	
	ParseJSon(root, jsRoot);
	if (jsRoot.isMember("params") == false)
	{
		std::cout<<"params not isMember"<<std::endl;
		return false;
	}

	std::string mac = jsRoot["sid"].asString();
	std::string Model = jsRoot["model"].asString();
	std::string cmd = jsRoot["cmd"].asString();
	Json::Value params = jsRoot["params"];

	std::vector<boost::tuple<std::string, std::string>> result;
	std::string key;
	
	int ii = 0;

	for (const auto & itt : m_rule)
	{
		result.emplace_back(boost::make_tuple(boost::get<0>(itt), ""));
		ii ++;
	}

	if (ii == 0 || (ii != 3 && m_type == LedType::LedTemp) ||
		(ii != 3 && m_type ==LedType::LedRGB) ||
		(ii != 4 && m_type ==LedType::LedRGBTemp))
	{
		std::cout<< "rule not match led type"<<std::endl;
		return false;
	}

	for (ii = 0; ii < params.size(); ii++)
	{
		for (auto & itt : result)
		{
			key = boost::get<0>(itt);
			std::cout<<"key: "<<key<<std::endl;

			if (params[ii].isMember(key))
			{
				 boost::get<1>(itt) = params[ii][key].asString();
				 commit = true;
			}
		}
	}

	if (commit == false)
	{
		std::cout<<"LedOutlet commit: false, message content error"<<std::endl;
		return false;
	}

	std::string name = "";
	std::string ledRGB = "";
	std::string level = "";
	std::string colorTemp = "";
	std::string status = "";
	int battery = 255;

	ii = result.size();
	_tColor color;


	XiaomiGateway* gw = static_cast<XiaomiGateway*>(miGateway);


	status = boost::get<1>(result[0]);
	if (!status.empty())
	{
		int Onoff = -1;
		if (status == "on")
		{
			Onoff == Color_LedOn;
		}
		else if (status == "off")
		{
			Onoff == Color_LedOff;
		}
		else
		{
			std::cout<<boost::get<0>(result[0])<<" value out of range"<<std::endl;
			return false;
		}
		gw->InsertUpdateRGBLight(mac, getUnit(), getSubType(), Onoff, level, color, battery);
	}
	
	switch(m_type)
	{
		case LedType::LedTemp:
			if (ii > 1)
			{
				level = boost::get<1>(result[1]);
				colorTemp = boost::get<1>(result[2]);
				if (!colorTemp.empty())
				{
					int cTemp = std::stoi(colorTemp) & 0xff;
					color.mode = ColorModeTemp;
					color.t = cTemp;
				}
			}
			
		break;
		case LedType::LedRGB:
			if (ii > 1)
			{
				level = boost::get<1>(result[1]);
				ledRGB = boost::get<1>(result[2]);
				if (!ledRGB.empty())
				{
					color.mode = ColorModeRGB;
					unsigned long rgb = std::stoul(ledRGB);
					color.r = ( rgb >> 16)	& 0xff;
					color.g = ( rgb >> 8)  & 0xff;
					color.b = rgb  & 0xff;
				}
			}
		break;
		case LedType::LedRGBTemp:
			if (ii > 2)
			{
				level = boost::get<1>(result[1]);
				ledRGB = boost::get<1>(result[2]);
				colorTemp = boost::get<1>(result[3]);
				if (!ledRGB.empty() && !colorTemp.empty())
				{
					unsigned long rgb = std::stoul(ledRGB);
					color.mode = ColorModeCustom;
					color.t = std::stoi(colorTemp) & 0xff;					
					color.r = ( rgb >> 16)  & 0xff;
					color.g = ( rgb >> 8)  & 0xff;
					color.b = rgb  & 0xff;
				}
				else if (!ledRGB.empty())
				{
					color.mode = ColorModeRGB;
					unsigned long rgb = std::stoul(ledRGB);
					color.r = ( rgb >> 16)	& 0xff;
					color.g = ( rgb >> 8)  & 0xff;
					color.b = rgb  & 0xff;
				}
				else if (!colorTemp.empty())
				{
					int cTemp = std::stoi(colorTemp) & 0xff;
					color.mode = ColorModeTemp;
					color.t = cTemp;
				}
			}
		break;
		default:
			std::cout<<"LedOutlet type not found"<<std::endl;
			return false;
		break;
	}
	gw->InsertUpdateRGBLight(mac, getUnit(), getSubType(), Color_SetColor, level, color, battery);
	return true;
}

bool LedOutlet::writeTo(const unsigned char* packet, int len, std::string& mac, std::string& model,std::string& gwMac,  std::string& key, void * miGateway) const
{
	std::cout<<"LedOutlet writeTo"<<std::endl;
	if (!miGateway)
	{
		return false;
	}
	
	const _tColorSwitch *xcmd = reinterpret_cast<const _tColorSwitch*>(packet);
	std::string control;
	unsigned int rgb;
	bool commit = false;

	Json::Value root;
	root["cmd"] = "write";
	root["sid"] = mac;
	root["model"] = model;
	root["key"] = "@gatewaykey";

	if (xcmd->command == gswitch_sOn)
	{
		root["params"][0]["light_rgb"] = 0x32ffffff;
		root["params"][1]["power_status"] = "on";
		commit = true;
	}
	else if (xcmd->command == gswitch_sOff)
	{
		root["params"][0]["light_rgb"] = 0;
		root["params"][1]["power_status"] = "off";
		commit = true;
	}
	else if (xcmd->command == Color_SetBrightnessLevel)
	{
		unsigned int bright = (unsigned int)xcmd->value; 
		bright = (bright > 100)? 100 : bright;
		root["params"][0]["light_level"] = bright;
		commit = true;
	}

	if (commit == true)
	{
		std::string message = JSonToRawString(root);
		XiaomiGateway* gw = static_cast<XiaomiGateway*>(miGateway);
		gw->SendMessageToGateway(message);
		return true;
	}

	if (xcmd->command != Color_SetColor)
	{
		std::cout<<"LedOutlet: not support colorswitch command"<<std::endl;
		return false;
	}

	_tColor color = xcmd->color;

	switch (xcmd->color.mode)
	{
		case ColorModeWhite:
		{
			unsigned int bright = xcmd->value;
			bright = (bright > 100)? 100 : bright;
			unsigned value = (bright << 24) | 0xffffff;
			root["params"][0]["light_rgb"] = value;
			root["params"][1]["light_level"] = bright;
		}
		break;
		case ColorModeTemp:
		{
			unsigned int bright = xcmd->value;
			unsigned int cTemp = color.t;

			bright = (bright > 100)? 100 : bright;
			cTemp = (cTemp > 100)? 100 : cTemp;

			root["params"][0]["color_temp"] = cTemp;
			root["params"][1]["light_level"] = bright;
		}
		break;			
		case ColorModeRGB:
		{
			unsigned char red = color.r;
			unsigned char green = color.g;
			unsigned char blue = color.b;
			unsigned int bright = xcmd->value;

			unsigned int value = (bright << 24) | (red << 16) | (green << 8) | (blue);
			root["params"][0]["light_rgb"] = value;
			root["params"][1]["light_level"] = bright;
		}
		break;
		case ColorModeCustom:
		{
			unsigned char red = color.r;
			unsigned char green = color.g;
			unsigned char blue = color.b;
			unsigned int bright = xcmd->value;
			unsigned int cTemp = color.t;
			
			unsigned int value = (bright << 24) | (red << 16) | (green << 8) | (blue);
			cTemp = (cTemp > 100)? 100 : cTemp;

			root["params"][0]["light_rgb"] = value;
			root["params"][1]["color_temp"] = cTemp;
			root["params"][2]["light_level"] = bright;
		}
		break;
		default:
			std::cout<<"LedOutlet: not support colorswitch model"<<std::endl;
			return false;
		break;

	}
	std::string message = JSonToRawString(root);
	XiaomiGateway* gw = static_cast<XiaomiGateway*>(miGateway);
	gw->SendMessageToGateway(message);

	return true;
}






}



