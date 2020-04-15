#include "Outlet.hpp"
#include <iostream>
#include <string>
#include <algorithm>
#include <ctype.h>


#include "OutletChild.hpp"
#include "../hardwaretypes.h"
#include "../../main/RFXNames.h"
#include "../../main/json_helper.h"



namespace XiaoMi{





OnOffOutlet::OnOffOutlet(int unit, int dir, std::initializer_list<Rule_OnOff> list)
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
	std::cout<<"commit"<<commit<<"result"<<result<<std::endl;

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




SensorBinOutlet::SensorBinOutlet(int unit, int swType, int dir, std::initializer_list<Rule_OnOff> list)
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
	std::cout<<"commit"<<commit<<"result"<<result<<std::endl;

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


}



