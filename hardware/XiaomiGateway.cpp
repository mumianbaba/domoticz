#include "stdafx.h"
#include "../hardware/hardwaretypes.h"
#include "../main/localtime_r.h"
#include "../main/Logger.h"
#include "../main/mainworker.h"
#include "../main/Helper.h"
#include "../main/SQLHelper.h"
#include "../main/WebServer.h"
#include "../webserver/cWebem.h"
#include "../main/json_helper.h"
#include "XiaomiGateway.h"
#include <openssl/aes.h>
#include <boost/asio.hpp>
#include <boost/bind.hpp>

#ifndef WIN32
#include <ifaddrs.h>
#endif

/*
Xiaomi (Aqara) makes a smart home gateway/hub that has support
for a variety of Xiaomi sensors.
They can be purchased on AliExpress or other stores at very
competitive prices.
Protocol is Zigbee and WiFi, and the gateway and
Domoticz need to be in the same network/subnet with multicast working
*/

namespace xiaomi{

const char* key_cmd = "cmd";
const char* key_model = "model";
const char* key_sid = "sid";
const char* key_params = "params";
const char* key_token = "token";
const char* key_dev_list ="dev_list";
const char* key_ip =	"ip";
const char* key_protocal ="protocal";
const char* key_port ="port";

const char* key_battery = "battery_voltage";

const char* cmd_whois = "whois";
const char* rsp_whois = "iam";

const char* cmd_discorey = "discovery";
const char* rsp_discorey = "discovery_rsp";

const char* cmd_read = "read";
const char* rsp_read = "read_rsp";

const char* cmd_write = "write";
const char* rsp_write = "write_rsp";

const char* rep_report = "report";
const char* rep_hbeat = "heartbeat";

}


 std::vector<boost::tuple<std::string, std::string, int, int, int, int, std::string> >  XiaomiGateway::m_SpDevice;


/*
	Ssid (short short id) 是为满足各种消息格式后，最终生成DeviceID(存在数据库，并在UI上展示)的id。
	例如sid 1234567890123456   --> short id -->90123456-->适配消息格式--->3456(温度传感器)--->(数据库)idx + "3456"+ DeviceType+DeviceSubType
	该函数将SsID转换成DeciceID，这里的DeciceID并不是数据库的主键，数据库的主键为ID，在api中为idx，从1开始依次递增。
*/
std::string XiaomiGateway::GetDeviceIdBySsid(const int devType, const int subType, unsigned int rowId)
{

	char szTmp[64];
	std::string DeviceId = "";

	memset(szTmp, 0, sizeof(szTmp));

	switch (devType)
	{
		case pTypeRAIN:
		case pTypeWIND:
		case pTypeTEMP:
		case pTypeHUM:
		case pTypeTEMP_HUM:
		case pTypeTEMP_HUM_BARO:
		case pTypeTEMP_BARO:
		case pTypeTEMP_RAIN:
		case pTypeUV:
		case pTypePOWER:
		{
			sprintf(szTmp, "%d", rowId);
		}
		break;


		case pTypeColorSwitch:
		case pTypeGeneralSwitch:
		{
			sprintf(szTmp, "%08X", rowId);
		}
		break;

		case pTypeGeneral:
		{
			if (
				(subType == sTypeVoltage) ||
				(subType == sTypeCurrent) ||
				(subType == sTypePercentage) ||
				(subType == sTypeWaterflow) ||
				(subType == sTypePressure) ||
				(subType == sTypeZWaveClock) ||
				(subType == sTypeZWaveThermostatMode) ||
				(subType == sTypeZWaveThermostatFanMode) ||
				(subType == sTypeZWaveThermostatOperatingState) ||
				(subType == sTypeFan) ||
				(subType == sTypeTextStatus) ||
				(subType == sTypeSoundLevel) ||
				(subType == sTypeBaro) ||
				(subType == sTypeDistance) ||
				(subType == sTypeSoilMoisture) ||
				(subType == sTypeCustom) ||
				(subType == sTypeKwh) ||
				(subType == sTypeZWaveAlarm)
				)
			{
				sprintf(szTmp, "%08X", rowId);
			}
			else
			{
				sprintf(szTmp, "%d", rowId);
			}
		}
		break;


		case pTypeHomeConfort:
		{
			unsigned char id1 = rowId;
			unsigned char id2 = rowId;
			unsigned char id3 = rowId;
			unsigned char id4 = rowId;

			sprintf(szTmp, "%02X%02X%02X%02X", id1, id2,id3, id4);
		}
		break;

		case pTypeUsage:
		case pTypeLux:
		{
			unsigned char id1 = (unsigned char)((rowId & 0xFF000000) >> 24);
			unsigned char id2 = (unsigned char)((rowId & 0xFF000000) >> 16);
			unsigned char id3 = (unsigned char)((rowId & 0xFF000000) >> 8);
			unsigned char id4 = (unsigned char)(rowId & 0xff);

			sprintf(szTmp, "%X%02X%02X%02X", id1, id2, id3, id4);
		}
		break;


		case pTypeWEATHER:
		{
			unsigned char id1 = 0;
			unsigned char id2 = 0;
			unsigned char id3 = 0;
			unsigned char id4 = (unsigned char)(rowId & 0xff);

			sprintf(szTmp, "%X%02X%02X%02X", id1, id2, id3, id4);
		}
		break;
			_log.Log(LOG_ERROR, "GetSsidByDeviceId unkown device type:%d subtype:%d\n", devType, subType);
		default:
		break;
	}
	DeviceId = szTmp;
	return DeviceId;
}

unsigned int XiaomiGateway::GetSsidByDeviceId(const int devType, const int subType, const std::string& deviceID)
{
	unsigned int ssid = 0;

	switch (devType)
	{
		case pTypeRAIN:
		case pTypeWIND:
		case pTypeTEMP:
		case pTypeHUM:
		case pTypeTEMP_HUM:
		case pTypeTEMP_HUM_BARO:
		case pTypeTEMP_BARO:
		case pTypeTEMP_RAIN:
		case pTypeUV:
		case pTypePOWER:
		{
			ssid = std::stoi(deviceID, 0, 10);
		}
		break;


		case pTypeColorSwitch:
		case pTypeGeneralSwitch:
		{
			ssid = std::stoi(deviceID, 0, 16);
		}
		break;

		case pTypeGeneral:
		{
			if (
				(subType == sTypeVoltage) ||
				(subType == sTypeCurrent) ||
				(subType == sTypePercentage) ||
				(subType == sTypeWaterflow) ||
				(subType == sTypePressure) ||
				(subType == sTypeZWaveClock) ||
				(subType == sTypeZWaveThermostatMode) ||
				(subType == sTypeZWaveThermostatFanMode) ||
				(subType == sTypeZWaveThermostatOperatingState) ||
				(subType == sTypeFan) ||
				(subType == sTypeTextStatus) ||
				(subType == sTypeSoundLevel) ||
				(subType == sTypeBaro) ||
				(subType == sTypeDistance) ||
				(subType == sTypeSoilMoisture) ||
				(subType == sTypeCustom) ||
				(subType == sTypeKwh) ||
				(subType == sTypeZWaveAlarm)
				)
			{
				ssid = std::stoi(deviceID, 0, 16);
			}
			else
			{
				ssid = std::stoi(deviceID, 0, 10);
			}
		}
		break;


		case pTypeHomeConfort:
		{
			ssid = std::stoi(deviceID, 0, 16);
		}
		break;

		case pTypeUsage:
		{
			ssid = std::stoi(deviceID, 0, 16);
		}
		break;

		case pTypeLux:
		{
			ssid = std::stoi(deviceID, 0, 16);
		}
		break;


		case pTypeWEATHER:
		{
			ssid = std::stoi(deviceID, 0, 16);
		}
		break;

		default:
			_log.Log(LOG_ERROR, "GetSsidByDeviceId unkown device type:%d subtype:%d\n", devType, subType);
		break;
	}
	return ssid;
}


int XiaomiGateway::GetUincastPort()
{
	return m_GatewayUPort;
}

void XiaomiGateway::SetUincastPort(int port)
{
	m_GatewayUPort = port;
}


/*
	return : short short id for cmd struct
*/
int XiaomiGateway::GetSsidBySid(const int devType, const int subType, const std::string& sid)
{
	int sID = GetShortID(sid);
	return GetSsidBySid(devType, subType, sID);
}
int XiaomiGateway::GetSsidBySid(const int devType, const int subType, const int sID)
{
	int sSID = sID;

	switch (devType)
	{
		case pTypeRAIN:
		case pTypeWIND:
		case pTypeTEMP:
		case pTypeHUM:
		case pTypeTEMP_HUM:
		case pTypeTEMP_HUM_BARO:
		case pTypeTEMP_BARO:
		case pTypeTEMP_RAIN:
		case pTypeUV:
		case pTypeFS20:
		case pTypeWEATHER:
		case pTypePOWER:
		{
			return sID & 0xffff;
		}
		break;

		case pTypeColorSwitch:
		case pTypeGeneralSwitch:
		{
			return sID & 0xffffffff;
		}
		break;

		case pTypeGeneral:
		{
			if (
				(subType == sTypeVoltage) ||
				(subType == sTypeCurrent) ||
				(subType == sTypePercentage) ||
				(subType == sTypeWaterflow) ||
				(subType == sTypePressure) ||
				(subType == sTypeZWaveClock) ||
				(subType == sTypeZWaveThermostatMode) ||
				(subType == sTypeZWaveThermostatFanMode) ||
				(subType == sTypeZWaveThermostatOperatingState) ||
				(subType == sTypeFan) ||
				(subType == sTypeTextStatus) ||
				(subType == sTypeSoundLevel) ||
				(subType == sTypeBaro) ||
				(subType == sTypeDistance) ||
				(subType == sTypeSoilMoisture) ||
				(subType == sTypeCustom) ||
				(subType == sTypeKwh) ||
				(subType == sTypeZWaveAlarm)
				)
			{
				return sID & 0xffffffff;
			}
			else
			{
				return sID & 0xfffff;
			}
		}
		break;


		case pTypeHomeConfort:
		{
			return sID & 0xffffffff;
		}
		break;

		case pTypeUsage:
		case pTypeLux:
		{
			return sID & 0xffffffff;
		}
		break;

		default:
			
		break;
	}

	return sSID;
}


std::vector<std::vector<std::string>>  XiaomiGateway::GetDeviceInfoByModel(const std::string & model)
{
	std::vector<std::vector<std::string>> result;
	std::string name = "";
	int devType = 0;
	int subType = 0;
	int swType = 0;
	int uint = 0;
	std::string opt = "";

	bool found = false;
	for (unsigned i = 0; i < m_SpDevice.size(); i++) {
		if (boost::get<SP_MODLE>(m_SpDevice[i]) == model) {
			name = boost::get<SP_NAME>(m_SpDevice[i]);
			devType = boost::get<SP_TYPE>(m_SpDevice[i]);
			subType = boost::get<SP_SUBTYPE>(m_SpDevice[i]);
			swType = boost::get<SP_SWTYPE>(m_SpDevice[i]);
			uint = boost::get<SP_UINT>(m_SpDevice[i]);
			opt = boost::get<SP_OPT>(m_SpDevice[i]);
			std::vector<std::string> values;

			values.push_back(name);
			values.push_back(std::to_string(devType));
			values.push_back(std::to_string(subType));
			values.push_back(std::to_string(swType));
			values.push_back(std::to_string(uint));
			values.push_back(opt);

			result.push_back(values);
			found = true;
		}
	}
	if (!found)
	{
		_log.Log(LOG_ERROR, "XiaomiGateway: GetDeviceInfoByModel not find model:%s\n", model.c_str());
	}
	return result;
}


void XiaomiGateway::SetDeviceInfo(const std::string & mac, const std::string & model)
{
	bool found = false;
	std::unique_lock<std::mutex> lock(m_mutex);
	for (unsigned i = 0; i < m_DevInfo.size(); i++) {
		if (boost::get<DY_MAC>(m_DevInfo[i]) == mac) {
			boost::get<DY_MODLE>(m_DevInfo[i]) = model;
			found = true;
			_log.Log(LOG_STATUS, "SetDeviceInfo update the modek %s", model.c_str());
		}
	}
	if (!found) {

		int type = 0;
		int subtype = 0;

		std::string devid = "";
		std::vector<std::vector<std::string>> result;

		result = GetDeviceInfoByModel(model);
		if (result.empty())
		{
			_log.Log(LOG_ERROR, "SetDeviceInfo no support the model %s", model.c_str());
			return;
		}
		type = atoi(result[0][1].c_str());
		subtype = atoi(result[0][2].c_str());
		
		int ssid = GetSsidBySid(type, subtype, mac);
		devid = GetDeviceIdBySsid(type, subtype, ssid);
		m_DevInfo.push_back(boost::make_tuple(ssid, model, mac, devid));
	}
}


/* 0:ssid  1:model  2:mac */
std::string XiaomiGateway::GetDeviceMac(const int ssid)
{
	bool found = false;
	std::string mac = "";
	std::unique_lock<std::mutex> lock(m_mutex);
	for (unsigned i = 0; i < m_DevInfo.size(); i++) {
		if (boost::get<DY_SSID>(m_DevInfo[i]) == ssid) {
			mac = boost::get<DY_MAC>(m_DevInfo[i]);
			found = true;
		}
	}
	if (!found)
	{
		_log.Log(LOG_ERROR, "XiaomiGateway: GetDeviceMac not find ssid:%d\n", ssid);
	}
	return mac;
}

/* 0:ssid  1:model  2:mac 3:devid */
std::string XiaomiGateway::GetDeviceModel(const int ssid)
{
	bool found = false;
	std::string model = "";
	std::unique_lock<std::mutex> lock(m_mutex);
	for (unsigned i = 0; i < m_DevInfo.size(); i++) {
		if (boost::get<DY_SSID>(m_DevInfo[i]) == ssid) {
			model = boost::get<DY_MODLE>(m_DevInfo[i]);
			found = true;
		}
	}
	if (!found)
	{
		_log.Log(LOG_ERROR, "XiaomiGateway: GetDeviceMac not find model ssid:%d\n", ssid);
	}
	return model;
}


/* 0:ssid  1:model  2:mac 3:devid */
std::string XiaomiGateway::GetDeviceModel(const std::string& mac)
{
	bool found = false;
	std::string model = "";
	std::unique_lock<std::mutex> lock(m_mutex);
	for (unsigned i = 0; i < m_DevInfo.size(); i++) {
		if (boost::get<DY_MAC>(m_DevInfo[i]) == mac) {
			model = boost::get<DY_MODLE>(m_DevInfo[i]);
			found = true;
		}
	}
	if (!found)
	{
		_log.Log(LOG_STATUS, "XiaomiGateway: GetDeviceMac not find model mac:%s\n", mac.c_str());
	}
	return model;
}

/* 0:ssid  1:model  2:mac 3:devid */
int XiaomiGateway::GetDeviceSsid(const std::string& mac)
{
	bool found = false;
	int ssid = 0;
	std::unique_lock<std::mutex> lock(m_mutex);
	for (unsigned i = 0; i < m_DevInfo.size(); i++) {
		if (boost::get<DY_MAC>(m_DevInfo[i]) == mac) {
			ssid  = boost::get<DY_SSID>(m_DevInfo[i]);
			found = true;
		}
	}
	if (!found)
	{
		_log.Log(LOG_STATUS, "XiaomiGateway: GetDeviceMac not find ssid mac:%s\n", mac.c_str());
	}
	return ssid;
}

/* 0:ssid  1:model  2:mac 3:devid */
std::string XiaomiGateway::GetDeviceId(const std::string& mac)
{
	bool found = false;
	std::string devid  = "";
	std::unique_lock<std::mutex> lock(m_mutex);
	for (unsigned i = 0; i < m_DevInfo.size(); i++) {
		if (boost::get<DY_MAC>(m_DevInfo[i]) == mac) {
			devid  = boost::get<DY_DEVICEID>(m_DevInfo[i]);
			found = true;
		}
	}
	if (!found)
	{
		_log.Log(LOG_STATUS, "XiaomiGateway: GetDeviceId not find mac:%s\n", mac.c_str());
	}
	return devid;
}


/* 0:ssid  1:model  2:mac 3:devid */
std::string XiaomiGateway::GetDeviceId(const int ssid)
{
	bool found = false;
	std::string devid  = "";
	std::unique_lock<std::mutex> lock(m_mutex);
	for (unsigned i = 0; i < m_DevInfo.size(); i++) {
		if (boost::get<DY_SSID>(m_DevInfo[i]) == ssid) {
			devid  = boost::get<DY_DEVICEID>(m_DevInfo[i]);
			found = true;
		}
	}
	if (!found)
	{
		_log.Log(LOG_STATUS, "XiaomiGateway: GetDeviceId not find ssid:%d\n", ssid);
	}
	return devid;
}



#define round(a) ( int ) ( a + .5 )
// Removing this vector and use unitcode to tell what kind of device each is
//std::vector<std::string> arrAqara_Wired_ID;

std::list<XiaomiGateway*> gatewaylist;
std::mutex gatewaylist_mutex;

XiaomiGateway * XiaomiGateway::GatewayByIp(std::string ip)
{
	XiaomiGateway * ret = NULL;
	{
		std::unique_lock<std::mutex> lock(gatewaylist_mutex);
		std::list<XiaomiGateway*>::iterator    it = gatewaylist.begin();
		for (; it != gatewaylist.end(); it++)
		{
			_log.Debug(DEBUG_HARDWARE, "XiaomiGateway: GatewayByIp gateway:%s find:%s", (*it)->GetGatewayIp().c_str(), ip.c_str());
			if ((*it)->GetGatewayIp() == ip)
			{
				ret = (*it);
				break;
			}
		};
	}
	return ret;
}

void XiaomiGateway::AddGatewayToList()
{
	XiaomiGateway * maingw = NULL;
	{
		std::unique_lock<std::mutex> lock(gatewaylist_mutex);
		std::list<XiaomiGateway*>::iterator    it = gatewaylist.begin();
		for (; it != gatewaylist.end(); it++)
		{
			if ((*it)->IsMainGateway())
			{
				maingw = (*it);
				break;
			}
		};

		if (!maingw)
		{
			SetAsMainGateway();
		}
		else
		{
			maingw->UnSetMainGateway();
		}

		gatewaylist.push_back(this);
	}

	if (maingw)
	{
		maingw->Restart();
	}
}

void XiaomiGateway::RemoveFromGatewayList()
{
	XiaomiGateway * maingw = NULL;
	{
		std::unique_lock<std::mutex> lock(gatewaylist_mutex);
		gatewaylist.remove(this);
		if (IsMainGateway())
		{
			UnSetMainGateway();

			if (gatewaylist.begin() != gatewaylist.end())
			{
				std::list<XiaomiGateway*>::iterator    it = gatewaylist.begin();
				maingw = (*it);
			}
		}
	}

	if (maingw)
	{
		maingw->Restart();
	}
}

// Use this function to get local ip addresses via getifaddrs when Boost.Asio approach fails
// Adds the addresses found to the supplied vector and returns the count
// Code from Stack Overflow - https://stackoverflow.com/questions/2146191
int XiaomiGateway::get_local_ipaddr(std::vector<std::string>& ip_addrs)
{
#ifdef WIN32
	return 0;
#else
	struct ifaddrs *myaddrs, *ifa;
	void *in_addr;
	char buf[64];
	int count = 0;

	if (getifaddrs(&myaddrs) != 0)
	{
		_log.Log(LOG_ERROR, "getifaddrs failed! (when trying to determine local ip address)");
		perror("getifaddrs");
		return 0;
	}

	for (ifa = myaddrs; ifa != NULL; ifa = ifa->ifa_next)
	{
		if (ifa->ifa_addr == NULL)
			continue;
		if (!(ifa->ifa_flags & IFF_UP))
			continue;

		switch (ifa->ifa_addr->sa_family)
		{
		case AF_INET:
		{
			struct sockaddr_in *s4 = (struct sockaddr_in *)ifa->ifa_addr;
			in_addr = &s4->sin_addr;
			break;
		}

		case AF_INET6:
		{
			struct sockaddr_in6 *s6 = (struct sockaddr_in6 *)ifa->ifa_addr;
			in_addr = &s6->sin6_addr;
			break;
		}

		default:
			continue;
		}

		if (!inet_ntop(ifa->ifa_addr->sa_family, in_addr, buf, sizeof(buf)))
		{
			_log.Log(LOG_ERROR, "Could not convert to IP address, inet_ntop failed for interface %s", ifa->ifa_name);
		}
		else
		{
			ip_addrs.push_back(buf);
			count++;
		}
	}

	freeifaddrs(myaddrs);
	return count;
#endif
}

void XiaomiGateway::RegisterSupportDevice(const std::string & model, const std::string & name,
											const int type, const int subtype, const int swtype, const int uint,
											const std::string & devopt)
{
	m_SpDevice.push_back(boost::make_tuple(model, name, type, subtype, swtype, uint, devopt));

}



XiaomiGateway::XiaomiGateway(const int ID)
{
	m_HwdID = ID;
	m_bDoRestart = false;
	m_ListenPort9898 = false;
	m_bDevInfoInited = false;
	m_GatewayUPort = 9494;
	m_SpDevice.clear();

	RegisterSupportDevice("vibration.aq1", "Aqara Vibration Sensor", pTypeGeneralSwitch, sSwitchTypeSelector ,STYPE_Selector, 1, "SelectorStyle:0;LevelNames:Off|touch|tilt|drop|angle");
	RegisterSupportDevice("remote.b186acn01", "Xiaomi Wireless Single Wall Switch", pTypeGeneralSwitch, sSwitchTypeSelector ,STYPE_Selector, 1, "SelectorStyle:0;LevelNames:Off|Click|Double Click|Long Click");
	RegisterSupportDevice("remote.b286acn01", "Xiaomi Wireless Dual Wall Switch", pTypeGeneralSwitch, sSwitchTypeSelector ,STYPE_Selector, 1, "SelectorStyle:0;LevelNames:Off|S1|S1 Double Click|S1 Long Click|S2|S2 Double Click|S2 Long Click|Both Click|Both Double Click|Both Long Click");
	RegisterSupportDevice("sensor_magnet.aq2", "Aqara Door Sensor", pTypeGeneralSwitch, sSwitchGeneralSwitch, STYPE_Contact, 1, "");
	RegisterSupportDevice("sensor_motion.aq2", "Aqara Motion Sensor", pTypeGeneralSwitch, sSwitchGeneralSwitch, STYPE_Motion, 1, "");
	RegisterSupportDevice("sensor_motion.aq2", "Aqara Motion Sensor", pTypeLux, sTypeLux, STYPE_OnOff, 1, "");
	RegisterSupportDevice("sensor_wleak.aq1", "Aqara wleak Sensor", pTypeGeneralSwitch, sSwitchGeneralSwitch, STYPE_SMOKEDETECTOR, 1, "");
	RegisterSupportDevice("sensor_switch.aq3", "Aqara Smart Push Button", pTypeGeneralSwitch, sSwitchTypeSelector ,STYPE_Selector, 1, "SelectorStyle:0;LevelNames:Off|Click|Double Click|Long Click|Shake");
	RegisterSupportDevice("weather", "Aqara TemHumPre Sensor", pTypeTEMP_HUM_BARO, sTypeTHB1, STYPE_OnOff, 1, "");
	RegisterSupportDevice("sensor_cube.aqgl01", "Aqara Cube", pTypeGeneralSwitch, sSwitchTypeSelector ,STYPE_Selector, 1, "SelectorStyle:0;LevelNames:Off|flip90|flip180|move|tap_twice|shake|swing|alert|free_fall|rotate");
	RegisterSupportDevice("ctrl_neutral1", "Xiaomi Wired Single Wall Switch", pTypeGeneralSwitch, sSwitchGeneralSwitch, STYPE_OnOff, 1, "");
	RegisterSupportDevice("ctrl_neutral2", "Xiaomi Wired Dual Wall Switch Ch1", pTypeGeneralSwitch, sSwitchGeneralSwitch, STYPE_OnOff, 2, "");
	RegisterSupportDevice("ctrl_neutral2", "Xiaomi Wired Dual Wall Switch Ch0", pTypeGeneralSwitch, sSwitchGeneralSwitch, STYPE_OnOff, 1, "");
	RegisterSupportDevice("plug.maeu01", "Xiaomi Smart Plug", pTypeGeneralSwitch, sSwitchGeneralSwitch, STYPE_OnOff, 1, "");
	RegisterSupportDevice("plug.maeu01", "Xiaomi Smart Plug Usage", pTypeGeneral, sTypeKwh, STYPE_OnOff, 1, "");
	RegisterSupportDevice("light.aqcn02", "Aqara ColorTemp Light", pTypeColorSwitch, sTypeColor_CW_WW, STYPE_Dimmer, 1, "");
}

XiaomiGateway::~XiaomiGateway(void)
{
}

int XiaomiGateway::WriteToGeneralSwitch(const tRBUF *pCmd,  Json::Value& json)
{
	unsigned char packettype = pCmd->ICMND.packettype;
	unsigned char subtype = pCmd->ICMND.subtype;
	if (packettype != pTypeGeneralSwitch){
		return -1;
	}

	_tGeneralSwitch *xcmd = (_tGeneralSwitch*)pCmd;

	std::string channel= "xxx";
	std::string commmand= "xxx";
	std::string mac = "0";


	int ssid = xcmd->id;
	mac = GetDeviceMac(ssid);
	json["sid"]=mac.c_str();
	json["model"] = GetDeviceModel(ssid);

	switch (subtype)
	{
		case sSwitchGeneralSwitch:
		{
			/* plug */
			if (xcmd->unitcode == 1)
			{
				commmand = (xcmd->cmnd == gswitch_sOn)?"on":"off";
				json["params"][0]["channel_0"] = commmand;
			}
			else if (xcmd->unitcode == 2)
			{
				commmand = (xcmd->cmnd == gswitch_sOn)?"on":"off";
				json["params"][0]["channel_1"] = commmand;
			}
			else if (xcmd->unitcode == 6)
			{
				json["sid"] = m_GatewaySID;
				json["model"] = "gateway";
				if (xcmd->cmnd == 1)
				{
					std::vector<std::vector<std::string> > result;
					result = m_sql.safe_query("SELECT Value FROM UserVariables WHERE (Name == 'XiaomiMP3')");
					json["params"][0]["mid"] = result[0][0].c_str();
				}
				else
				{
					json["params"][0]["mid"] ="10000";
				}
			}
			else if (xcmd->unitcode == 8)
			{
				commmand = (xcmd->cmnd == 1)?"on":"off";
				json["params"][0]["channel_0"] = commmand;
			}
			else if (xcmd->unitcode == 9)
			{
				commmand = (xcmd->cmnd == 1)?"on":"off";
				json["params"][0]["channel_0"] = commmand;
			}
			else if (xcmd->unitcode == 10)
			{
				commmand = (xcmd->cmnd == 1)?"on":"off";
				json["params"][0]["channel_0"] = commmand;
			}
			else if (xcmd->unitcode == 254)
			{
				commmand = (xcmd->cmnd == 1)?"on":"off";
				json["model"] = "gateway";
				json["params"][0]["join_permission"] = "yes";
			}
			else
			{
				std::cout<<"sSwitchGeneralSwitch uint unkown"<<std::endl;
				return -1;
			}
			return 0;
		}
		break;

		case sSwitchTypeSelector:
		{
			 if(xcmd->unitcode >= 3 && xcmd->unitcode <= 5)
			 {
				 int level = xcmd->level;
				 if (level == 0) { level = 10000; }
				 else {
					 if (xcmd->unitcode == 3) {
						 //Alarm Ringtone
						 if (level > 0) { level = (level / 10) - 1; }
					 }
					 else if (xcmd->unitcode == 4) {
						 //Alarm Clock
						 if (level > 0) { level = (level / 10) + 19; }
					 }
					 else if (xcmd->unitcode == 5) {
						 //Doorbell
						 if (level > 0) { level = (level / 10) + 9; }
					 }
				 }
				 json["params"][0]["vol"] =level;
				 return 0;
			 }
			 return 1;
		}
		break;
		case sSwitchBlindsT2:
		{

		}
		break;
	}
	return -1;
}

int XiaomiGateway::WriteToColorSwitch(const tRBUF *pCmd,  Json::Value& json)
{
	unsigned char packettype = pCmd->ICMND.packettype;
	unsigned char subtype = pCmd->ICMND.subtype;
	if (packettype != pTypeColorSwitch){
		return -1;
	}

	const _tColorSwitch *xcmd = reinterpret_cast<const _tColorSwitch*>(pCmd);
	std::string channel= "xxx";
	std::string commmand= "xxx";
	std::string mac = "0";
	std::string  model = "";

	int brightness = 0;
	int ir = 0;
	int ig = 0;
	int ib = 0;
	int cw = 0;
	int ww = 0;
	int ct = 0;

	int ssid = (unsigned int)xcmd->id;
	mac = GetDeviceMac(ssid);
	model = GetDeviceModel(ssid);
		
	json["sid"] = mac.c_str();
	json["model"] = model;
	if (xcmd->command == Color_LedOn)
	{
		brightness = 42;
		ir = 0xff;
		ig = 0xff;
		ib = 0xff;
		cw = 0;
		ww = 0;
		ct = 0;
		uint32_t value = (brightness << 24) | (ir << 16) | (ig << 8) | (ib);
		uint32_t cwww = cw << 8 | ww;
		json["params"][0]["light_rgb"] = value;
		return 0;
	}
	else if (xcmd->command == Color_LedOff)
	{
		brightness = 0;
		json["params"][0]["light_rgb"] = brightness;
		return 0;
	}
	else if (xcmd->command == Color_SetColor)
	{
		if (xcmd->color.mode == ColorModeWhite)
		{
			ir = 0xff;
			ig = 0xff;
			ib = 0xff;
			brightness = xcmd->value;
			uint32_t value = (brightness << 24) | (ir << 16) | (ig << 8) | (ib);
			json["params"][0]["light_rgb"] = value;
		}
		else if (xcmd->color.mode == ColorModeTemp)
		{
			cw = xcmd->color.cw;
			ww = xcmd->color.ww;
			ct = xcmd->color.t;
			brightness = xcmd->value;
			uint32_t cwww = cw << 8 | ww;
			unsigned int con_br = (brightness * 255) / 0x64;
			unsigned int con_ct = ct * (0xffff / 0xff);

			json["params"][0]["color_temp"] = con_ct;
			json["params"][1]["light_level"] = con_br;
		}
		else if (xcmd->color.mode == ColorModeRGB)
		{
			ir = xcmd->color.r;
			ig = xcmd->color.g;
			ib = xcmd->color.b;
			brightness = xcmd->value; //TODO: What is the valid range for XiaomiGateway, 0..100 or 0..255?

			uint32_t value = (brightness << 24) | (ir << 16) | (ig << 8) | (ib);
			json["params"][0]["light_rgb"] = value;
		}
		else if (xcmd->color.mode == ColorModeCustom)
		{
			ir = xcmd->color.r;
			ig = xcmd->color.g;
			ib = xcmd->color.b;
			cw = xcmd->color.cw;
			ww = xcmd->color.ww;
			ct = xcmd->color.t;
			brightness = xcmd->value; //TODO: What is the valid range for XiaomiGateway, 0..100 or 0..255?

			uint32_t value = (brightness << 24) | (ir << 16) | (ig << 8) | (ib);
			uint32_t cwww = cw  << 8 | ww;

			json["params"][0]["light_rgb"] = value;
			json["params"][1]["light_cwww"] = cwww;
			json["params"][2]["light_ct"] = ct;
		}
		else
		{
			_log.Log(LOG_STATUS, "XiaomiGateway: Set_Colour - Color mode %d is unhandled, if you have a suggestion for what it should do, please post on the Domoticz forum", xcmd->color.mode);
			return -1;
		}
		return 0;
	}
	else if ((xcmd->command == Color_SetBrightnessLevel) || (xcmd->command == Color_SetBrightUp) || (xcmd->command == Color_SetBrightDown))
	{
		//add the brightness
		if (xcmd->command == Color_SetBrightUp) {
			//brightness = std::min(m_GatewayBrightnessInt + 10, 255);
		}
		else if (xcmd->command == Color_SetBrightDown) {
			//brightness = std::max(m_GatewayBrightnessInt - 10, 0);
		}
		else {
			brightness = (int)xcmd->value; //TODO: What is the valid range for XiaomiGateway, 0..100 or 0..255?
		}

		uint32_t value = (brightness << 24) | (ir << 16) | (ig << 8) | (ib);

		unsigned int br = (255 * brightness) /0x64;
		json["params"][0]["light_rgb"] = value;
		json["params"][1]["light_level"] = br;
		return 0;
	}
	else if (xcmd->command == Color_SetColorToWhite)
	{
		//ignore Color_SetColorToWhite
	}
	else
	{
		_log.Log(LOG_ERROR, "XiaomiGateway: Unknown command %d", xcmd->command);
	}
	return -1;
}


int XiaomiGateway::WriteToMannageDeive(const tRBUF *pCmd,  Json::Value& json)
{
	unsigned char packettype = pCmd->ICMND.packettype;
	unsigned char subtype = pCmd->ICMND.subtype;
	if (packettype != pTypeMannageDevice){
		return -1;
	}
	std::string mac = "0";
	std::string mdmac = "0";

	int mdssid = 0;
	int mdid1 = pCmd->MANNAGE.id1;
	int mdid2 = pCmd->MANNAGE.id2;
	int mdid3 = pCmd->MANNAGE.id3;
	int mdid4 = pCmd->MANNAGE.id4;

	unsigned char mdtype = pCmd->MANNAGE.value1;
	unsigned char mdsubtype = pCmd->MANNAGE.value2;
	unsigned char mduint = pCmd->MANNAGE.value3;

	mdssid = ((mdid1 << 24) & 0xff000000) | ((mdid2 << 16) & 0xff0000) | ((mdid3 << 8) & 0xff00) | (mdid4 & 0xff);

	if (subtype == sTypeAddDevice)
	{
		const char* model = (const char*)&pCmd->MANNAGE.str[0];
		json["sid"] = m_GatewaySID;
		json["model"] = "gateway";
		json["params"][0]["join_permission"] = "yes";
		json["params"][1]["model"] = model;
	}
	else  if (subtype == sTypeRmDevice)
	{
		mdmac = GetDeviceMac(mdssid);
		json["sid"] = m_GatewaySID;
		json["params"][0]["remove_device"] = mdmac;
	}
	return 0;
}

bool XiaomiGateway::WriteToHardware(const char * pdata, const unsigned char length)
{
	const tRBUF *pCmd = reinterpret_cast<const tRBUF *>(pdata);
	unsigned char packettype = pCmd->ICMND.packettype;
	unsigned char subtype = pCmd->ICMND.subtype;
	bool result = true;
	std::string message = "";
	int ret = -1;

	if (m_GatewaySID == "") {
		m_GatewaySID = XiaomiGatewayTokenManager::GetInstance().GetSID(m_GatewayIp);
	}

	std::string mac = "";
	int ssid = 0;
	Json::Value jroot;

	jroot["cmd"] = "write";
	jroot["key"] = "@gatewaykey";

	ret = -1;
	if (pTypeGeneralSwitch == packettype) {
		ret = WriteToGeneralSwitch(pCmd, jroot);
	}
	else if (pTypeColorSwitch == packettype) {

		ret = WriteToColorSwitch(pCmd, jroot);
	}
	else if (pTypeMannageDevice == packettype)
	{
		ret = WriteToMannageDeive(pCmd, jroot);
	}
	if(0 == ret){
		message = JSonToRawString(jroot);
	}
	/* no need send message to gateway */
	/* parse cmd failed or not support cmd */
	else if (-1 == ret)
	{
		return false;
	}

	if (!message.empty()) {
		_log.Debug(DEBUG_HARDWARE, "XiaomiGateway: message: '%s'", message.c_str());
		result = SendMessageToGateway(message);
		if (result == false) {
			//send the message again
			_log.Log(LOG_STATUS, "XiaomiGateway: SendMessageToGateway failed on first attempt, will try again");
			sleep_milliseconds(100);
			result = SendMessageToGateway(message);
		}
	}
	return result;
}

bool XiaomiGateway::SendMessageToGateway(const std::string &controlmessage) {
	std::string message = controlmessage;
	bool result = true;
	boost::asio::io_service io_service;
	boost::asio::ip::udp::socket socket_(io_service, boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), 0));
	stdreplace(message, "@gatewaykey", GetGatewayKey());
	std::shared_ptr<std::string> message1(new std::string(message));
	boost::asio::ip::udp::endpoint remote_endpoint_;
	remote_endpoint_ = boost::asio::ip::udp::endpoint(boost::asio::ip::address::from_string(m_GatewayIp), m_GatewayUPort);
	socket_.send_to(boost::asio::buffer(*message1), remote_endpoint_);
	sleep_milliseconds(150);
	boost::array<char, 512> recv_buffer_;
	memset(&recv_buffer_[0], 0, sizeof(recv_buffer_));
#ifdef _DEBUG
	_log.Log(LOG_STATUS, "XiaomiGateway: request to %s - %s", m_GatewayIp.c_str(), message.c_str());
#endif
	while (socket_.available() > 0) {
		socket_.receive_from(boost::asio::buffer(recv_buffer_), remote_endpoint_);
		std::string receivedString(recv_buffer_.data());

#if 0
		Json::Value root;
		bool ret = ParseJSon(receivedString, root);
		if ((ret) && (root.isObject()))
		{
			std::string data = root["data"].asString();
			Json::Value root2;
			ret = ParseJSon(data.c_str(), root2);
			if ((ret) && (root2.isObject()))
			{
				std::string error = root2["error"].asString();
				if (error != "") {
					_log.Log(LOG_ERROR, "XiaomiGateway: unable to write command - %s", error.c_str());
					result = false;
				}
			}
		}
#endif
#ifdef _DEBUG
		_log.Log(LOG_STATUS, "XiaomiGateway: response %s", receivedString.c_str());
#endif
	}
	socket_.close();
	return result;
}

void XiaomiGateway::InsertUpdateTemperature(const std::string &nodeid, const std::string &Name, const float Temperature, const int battery)
{
	unsigned int sID = GetShortID(nodeid);
	if (sID > 0) {
		SendTempSensor(sID, battery, Temperature, Name);
	}
}

void XiaomiGateway::InsertUpdateHumidity(const std::string &nodeid, const std::string &Name, const int Humidity, const int battery)
{
	unsigned int sID = GetShortID(nodeid);
	if (sID > 0) {
		SendHumiditySensor(sID, battery, Humidity, Name);
	}
}

void XiaomiGateway::InsertUpdatePressure(const std::string &nodeid, const std::string &Name, const float Pressure, const int battery)
{
	unsigned int sID = GetShortID(nodeid);
	if (sID > 0) {
		SendPressureSensor((sID & 0xff00)>>8, sID & 0Xff, battery, Pressure, Name);
	}
}

void XiaomiGateway::InsertUpdateTempHumPressure(const std::string &nodeid, const std::string &Name, const std::string& Temperature, const std::string& Humidity, const std::string& Pressure, const int battery)
{
	bool res = true;
	unsigned int sID = GetShortID(nodeid);

	float lasttmep= 0.0;
	int lasthum = 0;
	int lasthumstatus = 0;
	int lastpre = 0;
	int lastorcast = 0;

	float tmep = 0.0;
	int   hum  = 0;
	float pre  = 0.0;

	tmep = (float)atof(Temperature.c_str()) / 100.0f;
	hum =  atoi(Humidity.c_str()) / 100;
	pre = static_cast<float>(atof(Pressure.c_str())) / 100.0f;

	if (Temperature == "" || Humidity == "" || Pressure == ""){

		std::string devid = GetDeviceId(nodeid);
		std::string svalue = "";
		int type    = pTypeTEMP_HUM_BARO;
		int subtype = sTypeTHB1;
		int nvalue  = 0;
		struct tm updatetime;

		res = m_sql.GetLastValue(m_HwdID, devid.c_str(), 1, type, subtype, nvalue, svalue, updatetime);
		if(false == res){
			_log.Log(LOG_ERROR, "get TempHumPressure svalue failed");
		}

		int n = sscanf(svalue.c_str(), "%f;%d;%d;%d;%d",  &lasttmep, &lasthum, &lasthumstatus, &lastpre, &lastorcast);
		if (n == 5){
			if (Temperature == ""){
				tmep = lasttmep;
			}
			if (Humidity == ""){
				hum = lasthum;
			}
			if (Pressure == ""){
				pre = lastpre;
			}
		}
	}

	int barometric_forcast = baroForecastNoInfo;
	if (pre < 1000)
		barometric_forcast = baroForecastRain;
	else if (pre < 1020)
		barometric_forcast = baroForecastCloudy;
	else if (pre < 1030)
		barometric_forcast = baroForecastPartlyCloudy;
	else
		barometric_forcast = baroForecastSunny;

	if (sID > 0) {
		SendTempHumBaroSensor(sID, battery, tmep, hum, pre, barometric_forcast, Name);
	}
}

void XiaomiGateway::InsertUpdateTempHum(const std::string &nodeid, const std::string &Name, const float Temperature, const int Humidity, const int battery)
{
	unsigned int sID = GetShortID(nodeid);
	if (sID > 0) {
		SendTempHumSensor(sID, battery, Temperature, Humidity, Name);
	}
}

void XiaomiGateway::InsertUpdateRGBLight(const std::string & nodeid, const std::string & Name, const unsigned char SubType, const unsigned char Mode, const std::string& Color, const std::string& Brightness, const bool bIsWhite,  const int battery)
{
	unsigned int sID = GetShortID(nodeid);
	if (sID > 0)
	{
		bool tIsOn = false;
		int lastLevel = 0;
		int nvalue = 0;
		int cmd = -1;
		int irgb = 0;
		int ibr = 0;

		irgb = atoi(Color.c_str());
		ibr = atoi(Brightness.c_str());

		std::cout<<"InsertUpdateRGBLight:irgb:"<<irgb<<"string ibr:"<<Brightness<<"  ibr:"<<ibr<<std::endl;

		std::vector<std::vector<std::string> > result;

		int ssid = GetSsidBySid(pTypeColorSwitch, SubType, sID);
		std::string devid = GetDeviceIdBySsid (pTypeColorSwitch, SubType, ssid);
		result = m_sql.safe_query("SELECT nValue, LastLevel FROM DeviceStatus WHERE (HardwareID==%d) AND (DeviceID=='%q') AND (Type==%d) AND (SubType==%d)", m_HwdID, devid.c_str(), pTypeColorSwitch, SubType);
		if (!result.empty())
		{
			nvalue = atoi(result[0][0].c_str());
			tIsOn = (nvalue != 0);
			lastLevel = atoi(result[0][1].c_str());

			if (tIsOn == true && Color != "" && irgb == 0)
			{
				cmd = Color_LedOff; //turn off
				ibr = lastLevel;
			}
			else if (tIsOn == false && Color != "" &&  irgb > 0)
			{
				cmd = Color_LedOn; //turn on
				ibr = lastLevel;
			}
			else if (tIsOn == true  && Brightness != "" && ibr != lastLevel)
			{
				cmd = Color_SetBrightnessLevel; //set bright
			}

			if (cmd != -1)
			{
				SendRGBWSwitch(sID, 1, SubType, Mode , 0, ibr, cmd, battery, Name);
			}
			std::cout<<"nvalue:"<<tIsOn<<"  lastlevel:"<<lastLevel<<std::endl;;
		}
		else
		{
			SendRGBWSwitch(sID, 1, SubType, Mode , 0, ibr, Color_LedOn, battery, Name);
		}
	}
}


void XiaomiGateway::InsertUpdateRGBGateway(const std::string & nodeid, const std::string & Name, const bool bIsOn, const int brightness, const int hue)
{
	if (nodeid.length() < 12) {
		_log.Log(LOG_ERROR, "XiaomiGateway: Node ID %s is too short", nodeid.c_str());
		return;
	}
	std::string str = nodeid.substr(4, 8);
	unsigned int sID;
	std::stringstream ss;
	ss << std::hex << str.c_str();
	ss >> sID;

	char szDeviceID[300];
	if (sID == 1)
		sprintf(szDeviceID, "%d", 1);
	else
		sprintf(szDeviceID, "%08X", (unsigned int)sID);

	int lastLevel = 0;
	int nvalue = 0;
	bool tIsOn = !(bIsOn);
	std::vector<std::vector<std::string> > result;
	result = m_sql.safe_query("SELECT nValue, LastLevel FROM DeviceStatus WHERE (HardwareID==%d) AND (DeviceID=='%q') AND (Type==%d) AND (SubType==%d)", m_HwdID, szDeviceID, pTypeColorSwitch, sTypeColor_RGB_W);

	if (result.empty())
	{
		_log.Log(LOG_STATUS, "XiaomiGateway: New Gateway Found (%s/%s)", str.c_str(), Name.c_str());
		int cmd = Color_LedOn;
		if (!bIsOn) {
			cmd = Color_LedOff;
		}
		_tColorSwitch ycmd;
		ycmd.subtype = sTypeColor_RGB_W;
		ycmd.id = sID;
		//ycmd.dunit = 0;
		ycmd.value = brightness;
		ycmd.command = cmd;
		m_mainworker.PushAndWaitRxMessage(this, (const unsigned char *)&ycmd, NULL, -1);
		m_sql.safe_query("UPDATE DeviceStatus SET Name='%q', SwitchType=%d, LastLevel=%d WHERE(HardwareID == %d) AND (DeviceID == '%s') AND (Type == %d)", Name.c_str(), (STYPE_Dimmer), brightness, m_HwdID, szDeviceID, pTypeColorSwitch);

	}
	else {
		nvalue = atoi(result[0][0].c_str());
		tIsOn = (nvalue != 0);
		lastLevel = atoi(result[0][1].c_str());
		if ((bIsOn != tIsOn) || (brightness != lastLevel))
		{
			int cmd = Color_LedOn;
			if (!bIsOn) {
				cmd = Color_LedOff;
			}
			_tColorSwitch ycmd;
			ycmd.subtype = sTypeColor_RGB_W;
			ycmd.id = sID;
			ycmd.value = brightness;
			ycmd.command = cmd;
			m_mainworker.PushAndWaitRxMessage(this, (const unsigned char *)&ycmd, NULL, -1);
		}
	}
}

void XiaomiGateway::InsertUpdateSwitch(const std::string &nodeid, const std::string &Name, const bool bIsOn, const _eSwitchType switchtype, const int unitcode, const int level, const std::string &messagetype, const std::string &load_power, const std::string &power_consumed, const int battery)
{
	unsigned int sID = GetShortID(nodeid);

	char szTmp[300];
	if (sID == 1)
		sprintf(szTmp, "%d", 1);
	else
		sprintf(szTmp, "%08X", (unsigned int)sID);
	std::string ID = szTmp;

	_tGeneralSwitch xcmd;
	xcmd.len = sizeof(_tGeneralSwitch) - 1;
	xcmd.id = sID;
	xcmd.type = pTypeGeneralSwitch;
	xcmd.subtype = sSwitchGeneralSwitch;
	xcmd.unitcode = unitcode;

	xcmd.level = level;
	int customimage = 0;

	if ((xcmd.unitcode > 2) && (xcmd.unitcode < 8)) {
		customimage = 8; //speaker
	}

	if (bIsOn) {
		xcmd.cmnd = gswitch_sOn;
	}
	else {
		xcmd.cmnd = gswitch_sOff;
	}
	if (switchtype == STYPE_Selector) {
		xcmd.subtype = sSwitchTypeSelector;
		if (level > 0) {
			xcmd.cmnd = gswitch_sSetLevel;
			xcmd.level = level;
		}
	}
	else if (switchtype == STYPE_SMOKEDETECTOR) {
		xcmd.level = level;
	}
	else if (switchtype == STYPE_BlindsPercentage) {
		xcmd.level = level;
		xcmd.subtype = sSwitchBlindsT2;
		xcmd.cmnd = gswitch_sSetLevel;
	}

	//check if this switch is already in the database
	std::vector<std::vector<std::string> > result;

	// block this device if it is already added for another gateway hardware id
	result = m_sql.safe_query("SELECT nValue FROM DeviceStatus WHERE (HardwareID!=%d) AND (DeviceID=='%q') AND (Type==%d) AND (Unit == '%d')", m_HwdID, ID.c_str(), xcmd.type, xcmd.unitcode);
	if (!result.empty()) {
		return;
	}

	result = m_sql.safe_query("SELECT nValue, BatteryLevel FROM DeviceStatus WHERE (HardwareID==%d) AND (DeviceID=='%q') AND (Type==%d) AND (Unit == '%d')", m_HwdID, ID.c_str(), xcmd.type, xcmd.unitcode);
	if (result.empty())
	{
		_log.Log(LOG_STATUS, "XiaomiGateway: New %s Found (%s)", Name.c_str(), nodeid.c_str());

		m_mainworker.PushAndWaitRxMessage(this, (const unsigned char *)&xcmd, NULL, battery);
		if (customimage == 0) {
			if (switchtype == STYPE_OnOff) {
				customimage = 1; //wall socket
			}
			else if (switchtype == STYPE_Selector) {
				customimage = 9;
			}
		}

		m_sql.safe_query("UPDATE DeviceStatus SET Name='%q', SwitchType=%d, CustomImage=%i, Used=%i WHERE(HardwareID == %d) AND (DeviceID == '%q') AND (Unit == '%d')", Name.c_str(), (switchtype), customimage, 1, m_HwdID, ID.c_str(), xcmd.unitcode);
		if (switchtype == STYPE_Selector) {
			result = m_sql.safe_query("SELECT ID FROM DeviceStatus WHERE (HardwareID==%d) AND (DeviceID=='%q') AND (Type==%d) AND (Unit == '%d')", m_HwdID, ID.c_str(), xcmd.type, xcmd.unitcode);
			if (!result.empty()) {
				std::string Idx = result[0][0];
				if (Name == "Xiaomi Wireless Switch") {
					m_sql.SetDeviceOptions(atoi(Idx.c_str()), m_sql.BuildDeviceOptions("SelectorStyle:0;LevelNames:Off|Click|Double Click|Long Click|Long Click Release", false));
				}
				else if (Name == "Xiaomi Square Wireless Switch") {
					// click/double click
					m_sql.SetDeviceOptions(atoi(Idx.c_str()), m_sql.BuildDeviceOptions("SelectorStyle:0;LevelNames:Off|Click|Double Click", false));
				}
				else if (Name == "Xiaomi Smart Push Button") {
					// click/double click/long Click/share
					m_sql.SetDeviceOptions(atoi(Idx.c_str()), m_sql.BuildDeviceOptions("SelectorStyle:0;LevelNames:Off|Click|Double Click|Long Click|Shake", false));
				}
				else if (Name == "Xiaomi Cube") {
					// flip90/flip180/move/tap_twice/shake/swing/alert/free_fall
					m_sql.SetDeviceOptions(atoi(Idx.c_str()), m_sql.BuildDeviceOptions("SelectorStyle:0;LevelNames:Off|flip90|flip180|move|tap_twice|shake|swing|alert|free_fall|clock_wise|anti_clock_wise", false));
				}
				else if (Name == "Aqara Cube") {
					// flip90/flip180/move/tap_twice/shake/swing/alert/free_fall/rotate
					m_sql.SetDeviceOptions(atoi(Idx.c_str()), m_sql.BuildDeviceOptions("SelectorStyle:0;LevelNames:Off|flip90|flip180|move|tap_twice|shake|swing|alert|free_fall|rotate", false));
				}
				else if (Name == "Aqara Vibration Sensor") {
					// tilt/vibrate/free fall
					m_sql.SetDeviceOptions(atoi(Idx.c_str()), m_sql.BuildDeviceOptions("SelectorStyle:0;LevelNames:Off|touch|tilt|drop|angle", false));
				}
				else if (Name == "Xiaomi Wireless Dual Wall Switch") {
					//for Aqara wireless switch, 2 buttons support
					m_sql.SetDeviceOptions(atoi(Idx.c_str()), m_sql.BuildDeviceOptions("SelectorStyle:0;LevelNames:Off|S1|S1 Double Click|S1 Long Click|S2|S2 Double Click|S2 Long Click|Both Click|Both Double Click|Both Long Click", false));
				}
				else if (Name == "Xiaomi Wired Single Wall Switch") {
					//for Aqara wired switch, single button support
					m_sql.SetDeviceOptions(atoi(Idx.c_str()), m_sql.BuildDeviceOptions("SelectorStyle:0;LevelNames:Off|Switch1 On|Switch1 Off", false));
				}
				else if (Name == "Xiaomi Wireless Single Wall Switch") {
					//for Aqara wireless switch, single button support
					m_sql.SetDeviceOptions(atoi(Idx.c_str()), m_sql.BuildDeviceOptions("SelectorStyle:0;LevelNames:Off|Click|Double Click|Long Click", false));
				}
				else if (Name == "Xiaomi Gateway Alarm Ringtone") {
					//for the Gateway Audio
					m_sql.SetDeviceOptions(atoi(Idx.c_str()), m_sql.BuildDeviceOptions("SelectorStyle:1;LevelNames:Off|Police siren 1|Police siren 2|Accident tone|Missle countdown|Ghost|Sniper|War|Air Strike|Barking dogs", false));
				}
				else if (Name == "Xiaomi Gateway Alarm Clock") {
					//for the Gateway Audio
					m_sql.SetDeviceOptions(atoi(Idx.c_str()), m_sql.BuildDeviceOptions("SelectorStyle:1;LevelNames:Off|MiMix|Enthusiastic|GuitarClassic|IceWorldPiano|LeisureTime|Childhood|MorningStreamlet|MusicBox|Orange|Thinker", false));
				}
				else if (Name == "Xiaomi Gateway Doorbell") {
					//for the Gateway Audio
					m_sql.SetDeviceOptions(atoi(Idx.c_str()), m_sql.BuildDeviceOptions("SelectorStyle:1;LevelNames:Off|Doorbell ring tone|Knock on door|Hilarious|Alarm clock", false));
				}
			}
		}
		else if (switchtype == STYPE_OnOff && Name == "Xiaomi Gateway MP3") {
			std::string errorMessage;
			m_sql.AddUserVariable("XiaomiMP3", USERVARTYPE_INTEGER, "10001", errorMessage);
		}

		if ((Name == "Xiaomi Smart Plug") || (Name == "Xiaomi Smart Wall Plug")) {
			if (load_power != "" || power_consumed != "") {
				double power = atof(load_power.c_str());
				double consumed = atof(power_consumed.c_str()) / 1000;
				SendKwhMeter(sID>>8 , sID & 0xff, 255, power, consumed, "Xiaomi Smart Plug Usage");
				int ssid = GetSsidBySid(pTypeGeneral, sTypeKwh,sID);
			}
		}
	}
	else {
		int nvalue = atoi(result[0][0].c_str());
		int BatteryLevel = atoi(result[0][1].c_str());

		if (messagetype == "heartbeat") {
			if (battery != 255) {
				BatteryLevel = battery;
				m_sql.safe_query("UPDATE DeviceStatus SET BatteryLevel=%d WHERE(HardwareID == %d) AND (DeviceID == '%q') AND (Unit == '%d')", BatteryLevel, m_HwdID, ID.c_str(), xcmd.unitcode);
			}
		}
		else {
			if ((bIsOn == false && nvalue >= 1) || (bIsOn == true) || (Name == "Xiaomi Wired Dual Wall Switch") || (Name == "Xiaomi Wired Single Wall Switch") || (Name == "Xiaomi Curtain")) {
				m_mainworker.PushAndWaitRxMessage(this, (const unsigned char *)&xcmd, NULL, BatteryLevel);
			}
		}
		if ((Name == "Xiaomi Smart Plug") || (Name == "Xiaomi Smart Wall Plug")) {
			if (load_power != "" || power_consumed != "") {
				double power = atof(load_power.c_str());
				double consumed = atof(power_consumed.c_str()) / 1000;
				SendKwhMeter(sID>>8, sID & 0xff, 255, power, consumed, "Xiaomi Smart Plug Usage");
			}
		}
	}
}

void XiaomiGateway::InsertUpdateCubeText(const std::string & nodeid, const std::string & Name, const std::string &degrees)
{
	unsigned int sID = GetShortID(nodeid);
	if (sID > 0) {
		SendTextSensor(sID>>8, sID & 0xff, 255, degrees.c_str(), Name);
	}
}

void XiaomiGateway::InsertUpdateVoltage(const std::string & nodeid, const std::string & Name, const int VoltageLevel)
{
	if (VoltageLevel < 3600) {
		unsigned int sID = GetShortID(nodeid);
		if (sID > 0) {
			int percent = ((VoltageLevel - 2200) / 10);
			float voltage = (float)VoltageLevel / 1000;
			SendVoltageSensor(sID>>8, sID & 0xff, percent, voltage, "Xiaomi Voltage");
		}
	}
}

void XiaomiGateway::InsertUpdateLux(const std::string & nodeid, const std::string & Name, const int Illumination, const int battery)
{
	unsigned int sID = GetShortID(nodeid);
	if (sID > 0) {
		float lux = (float)Illumination;
		SendLuxSensor(sID, 1, battery, lux, Name);
	}
}

bool XiaomiGateway::StartHardware()
{
	RequestStart();

	m_bDoRestart = false;
	m_bDevInfoInited = false;

	//force connect the next first time
	m_bIsStarted = true;

	m_GatewayMusicId = "10000";
	m_GatewayVolume = "20";

	std::vector<std::vector<std::string> > result;
	result = m_sql.safe_query("SELECT Password, Address FROM Hardware WHERE Type=%d AND ID=%d AND Enabled=1", HTYPE_XiaomiGateway, m_HwdID);

	if (result.empty())
		return false;

	m_GatewayPassword = result[0][0].c_str();
	m_GatewayIp = result[0][1].c_str();

	m_GatewayRgbR = 255;
	m_GatewayRgbG = 255;
	m_GatewayRgbB = 255;
	m_GatewayRgbCW = 255;
	m_GatewayRgbWW = 255;
	m_GatewayRgbCT = 255;
	m_GatewayBrightnessInt = 100;

	//check for presence of Xiaomi user variable to enable message output
	m_OutputMessage = false;
	result = m_sql.safe_query("SELECT Value FROM UserVariables WHERE (Name == 'XiaomiMessage')");
	if (!result.empty()) {
		m_OutputMessage = true;
	}
	//check for presence of Xiaomi user variable to enable additional voltage devices
	m_IncludeVoltage = false;
	result = m_sql.safe_query("SELECT Value FROM UserVariables WHERE (Name == 'XiaomiVoltage')");
	if (!result.empty()) {
		m_IncludeVoltage = true;
	}
	_log.Log(LOG_STATUS, "XiaomiGateway (ID=%d): Delaying worker startup...", m_HwdID);
	sleep_seconds(5);

	XiaomiGatewayTokenManager::GetInstance();

	AddGatewayToList();

	if (m_ListenPort9898)
	{
		_log.Log(LOG_STATUS, "XiaomiGateway (ID=%d): Selected as main Gateway", m_HwdID);
	}

	//Start worker thread
	m_thread = std::shared_ptr<std::thread>(new std::thread(&XiaomiGateway::Do_Work, this));
	SetThreadNameInt(m_thread->native_handle());

	return (m_thread != nullptr);
}

bool XiaomiGateway::StopHardware()
{
	if (m_thread)
	{
		RequestStop();
		m_thread->join();
		m_thread.reset();
	}
	m_bIsStarted = false;
	return true;
}

void XiaomiGateway::Do_Work()
{
	_log.Log(LOG_STATUS, "XiaomiGateway (ID=%d): Worker started...", m_HwdID);
	boost::asio::io_service io_service;
	//find the local ip address that is similar to the xiaomi gateway
	try {
		boost::asio::ip::udp::resolver resolver(io_service);
		boost::asio::ip::udp::resolver::query query(boost::asio::ip::udp::v4(), m_GatewayIp, "");
		boost::asio::ip::udp::resolver::iterator endpoints = resolver.resolve(query);
		boost::asio::ip::udp::endpoint ep = *endpoints;
		boost::asio::ip::udp::socket socket(io_service);
		socket.connect(ep);
		boost::asio::ip::address addr = socket.local_endpoint().address();
		std::string compareIp = m_GatewayIp.substr(0, (m_GatewayIp.length() - 3));
		std::size_t found = addr.to_string().find(compareIp);
		if (found != std::string::npos) {
			m_LocalIp = addr.to_string();
			_log.Log(LOG_STATUS, "XiaomiGateway (ID=%d): Using %s for local IP address.", m_HwdID, m_LocalIp.c_str());
		}
	}
	catch (std::exception& e) {
		_log.Log(LOG_STATUS, "XiaomiGateway (ID=%d): Could not detect local IP address using Boost.Asio: %s", m_HwdID, e.what());
	}

	// try finding local ip using ifaddrs when Boost.Asio fails
	if (m_LocalIp == "") {
		try {
			// get first 2 octets of Xiaomi gateway ip to search for similar ip address
			std::string compareIp = m_GatewayIp.substr(0, (m_GatewayIp.length() - 3));
			_log.Log(LOG_STATUS, "XiaomiGateway (ID=%d): XiaomiGateway IP address starts with: %s", m_HwdID, compareIp.c_str());

			std::vector<std::string> ip_addrs;
			if (XiaomiGateway::get_local_ipaddr(ip_addrs) > 0)
			{
				for (const std::string &addr : ip_addrs)
				{
					std::size_t found = addr.find(compareIp);
					if (found != std::string::npos)
					{
						m_LocalIp = addr;
						_log.Log(LOG_STATUS, "XiaomiGateway (ID=%d): Using %s for local IP address.", m_HwdID, m_LocalIp.c_str());
						break;
					}
				}
			}
			else
			{
				_log.Log(LOG_STATUS, "XiaomiGateway (ID=%d): Could not find local IP address with ifaddrs", m_HwdID);
			}
		}
		catch (std::exception& e) {
			_log.Log(LOG_STATUS, "XiaomiGateway (ID=%d): Could not find local IP address with ifaddrs: %s", m_HwdID, e.what());
		}
	}

	XiaomiGateway::xiaomi_udp_server udp_server(io_service, m_HwdID, m_GatewayIp, m_LocalIp, m_ListenPort9898, m_OutputMessage, m_IncludeVoltage, this);
	boost::thread bt;
	boost::thread bt2;
	if (m_ListenPort9898) {
		bt = boost::thread(boost::bind(&boost::asio::io_service::run, &io_service));
		SetThreadName(bt.native_handle(), "XiaomiGatewayIO");

		bt2 = boost::thread(boost::bind(&XiaomiGateway::xiaomi_udp_server::whois, &udp_server));
		SetThreadName(bt2.native_handle(), "XiaomiGatewayWhois");
	}

	int sec_counter = 0;
	while (!IsStopRequested(1000))
	{
		sec_counter++;
		if (sec_counter % 12 == 0) {
			m_LastHeartbeat = mytime(NULL);
		}
		if (sec_counter % 60 == 0)
		{
			//_log.Log(LOG_STATUS, "sec_counter %d", sec_counter);
		}
	}

	bt2.interrupt();
	bt2.join();
	io_service.stop();
	RemoveFromGatewayList();
	_log.Log(LOG_STATUS, "XiaomiGateway (ID=%d): stopped", m_HwdID);
}

std::string XiaomiGateway::GetGatewayKey()
{
#ifdef WWW_ENABLE_SSL
	const unsigned char *key = (unsigned char *)m_GatewayPassword.c_str();
	unsigned char iv[AES_BLOCK_SIZE] = { 0x17, 0x99, 0x6d, 0x09, 0x3d, 0x28, 0xdd, 0xb3, 0xba, 0x69, 0x5a, 0x2e, 0x6f, 0x58, 0x56, 0x2e };
	std::string token = XiaomiGatewayTokenManager::GetInstance().GetToken(m_GatewayIp);
	unsigned char *plaintext = (unsigned char *)token.c_str();
	unsigned char ciphertext[128];

	AES_KEY encryption_key;
	AES_set_encrypt_key(key, 128, &(encryption_key));
	AES_cbc_encrypt((unsigned char *)plaintext, ciphertext, sizeof(plaintext) * 8, &encryption_key, iv, AES_ENCRYPT);

	char gatewaykey[128];
	for (int i = 0; i < 16; i++)
	{
		sprintf(&gatewaykey[i * 2], "%02X", ciphertext[i]);
	}
//#ifdef _DEBUG
	_log.Log(LOG_STATUS, "XiaomiGateway: GetGatewayKey Password - %s, token=%s", m_GatewayPassword.c_str(), token.c_str());
	_log.Log(LOG_STATUS, "XiaomiGateway: GetGatewayKey key - %s", gatewaykey);
//#endif
	return gatewaykey;
#else
	_log.Log(LOG_ERROR, "XiaomiGateway: GetGatewayKey NO SSL AVAILABLE");
	return std::string("");
#endif
}

unsigned int XiaomiGateway::GetShortID(const std::string & nodeid)
{

	if (nodeid.length() < 12) {
		_log.Log(LOG_ERROR, "XiaomiGateway: Node ID %s is too short", nodeid.c_str());
		return -1;
	}
	uint64_t sID = std::strtoull(nodeid.c_str(), NULL, 16);
	std::cout<<"nodeid:"<<nodeid<<"-----sID:"<<sID<<std::endl;
	return (sID & 0xffffffff);
}




void XiaomiGateway::CreateNewDevice()
{

	int ii;
	int ssid;
	std::string model = "";
	std::string mac = "";
	
	std::unique_lock<std::mutex> lock(m_mutex);
	for (ii = 0; ii < m_DevInfo.size(); ii++)
	{
		model = boost::get<DY_MODLE>(m_DevInfo[ii]);
		mac = boost::get<DY_MAC>(m_DevInfo[ii]);
		CreateNewDevice(model, mac);
	}
}


void XiaomiGateway::CreateNewDevice(const std::string& model,const std::string& mac)
{
	int ii;
	int type , subtype, switchtype, uint;
	uint64_t DeviceRowIdx = (uint64_t)-1;

	std::string name = "";
	std::string devopt = "";
	std::string opts = "";
	std::string devid = "";

	int ssid  = 0;
	std::vector<std::vector<std::string>> result;
	result = GetDeviceInfoByModel(model);

	for (ii = 0; ii < result.size(); ii++)
	{
		name = result[ii][0];
		type       = atoi(result[ii][1].c_str());
		subtype    = atoi(result[ii][2].c_str());
		switchtype = atoi(result[ii][3].c_str());
		uint       = atoi(result[ii][4].c_str());
		devopt = result[ii][5];
		ssid = GetSsidBySid(type, subtype, mac);
		devid = GetDeviceIdBySsid(type, subtype, ssid);
		if(m_sql.DoesDeviceExist(m_HwdID, devid.c_str(), uint, type, subtype) == false)
		{
			bool bPrevAcceptNewHardware = m_sql.m_bAcceptNewHardware;
			m_sql.m_bAcceptNewHardware = true;
			DeviceRowIdx = m_sql.CreateDevice(m_HwdID, type, subtype, name, ssid, opts);
			m_sql.m_bAcceptNewHardware = bPrevAcceptNewHardware;
			if (DeviceRowIdx != (uint64_t)-1)
			{
				int customimage = 0;
				if (customimage == 0)
				{
					if (switchtype == STYPE_OnOff)
					{
						customimage = 1; //wall socket
					}
					else if (switchtype == STYPE_Selector)
					{
						customimage = 9;
					}
				}
				//m_sql.UpdateDeviceValue("Unit", uint, std::to_string(DeviceRowIdx));
				//m_sql.UpdateDeviceValue("SwitchType", switchtype, std::to_string(DeviceRowIdx));
				//m_sql.UpdateDeviceValue("CustomImage", customimage, std::to_string(DeviceRowIdx));
				//m_sql.UpdateDeviceValue("Model", model.c_str(), std::to_string(DeviceRowIdx));
				//m_sql.UpdateDeviceValue("Mac", mac.c_str(), std::to_string(DeviceRowIdx));
				m_sql.safe_query("UPDATE DeviceStatus SET Unit=%d, SwitchType=%d, CustomImage=%i, Model='%q', Mac='%q' WHERE(HardwareID == %d) AND (ID = %" PRIu64 ")", uint, switchtype, customimage,  model.c_str(), mac.c_str(), m_HwdID, DeviceRowIdx);

				if (devopt != "")
				{
					m_sql.SetDeviceOptions(DeviceRowIdx, m_sql.BuildDeviceOptions(devopt, false));
				}
				_log.Log(LOG_STATUS,  "add new device successful model:%s, ssid:%08X", model.c_str(), ssid);
			}
			else
			{
				_log.Log(LOG_ERROR,  "add new device failed model:%s, ssid:%08X", model.c_str(), ssid);
			}
		}
	}	
}

XiaomiGateway::xiaomi_udp_server::xiaomi_udp_server(boost::asio::io_service& io_service, int m_HwdID, const std::string &gatewayIp, const std::string &localIp, const bool listenPort9898, const bool outputMessage, const bool includeVoltage, XiaomiGateway *parent)
	: socket_(io_service, boost::asio::ip::udp::v4())
{
	m_uincastport = 0;
	m_HardwareID = m_HwdID;
	m_XiaomiGateway = parent;
	m_gatewayip = gatewayIp;
	m_localip = localIp;
	m_OutputMessage = outputMessage;
	m_IncludeVoltage = includeVoltage;
	if (listenPort9898) {
		try {
			socket_.set_option(boost::asio::ip::udp::socket::reuse_address(true));
			_log.Log(LOG_ERROR, "XiaomiGateway: xiaomi_udp_server m_localip： %s", m_localip.c_str());
			if (m_localip != "") {
				boost::system::error_code ec;
				boost::asio::ip::address listen_addr = boost::asio::ip::address::from_string(m_localip, ec);
				boost::asio::ip::address mcast_addr = boost::asio::ip::address::from_string("224.0.0.50", ec);
				boost::asio::ip::udp::endpoint listen_endpoint(mcast_addr, 9898);

				socket_.bind(boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), 9898));
				std::shared_ptr<std::string> message(new std::string("{\"cmd\":\"whois\"}"));
				boost::asio::ip::udp::endpoint remote_endpoint;
				remote_endpoint = boost::asio::ip::udp::endpoint(mcast_addr, 4321);
				socket_.send_to(boost::asio::buffer(*message), remote_endpoint);
				socket_.set_option(boost::asio::ip::multicast::join_group(mcast_addr.to_v4(), listen_addr.to_v4()), ec);
				socket_.bind(listen_endpoint, ec);
			}
			else {
				socket_.bind(boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), 9898));
				std::shared_ptr<std::string> message(new std::string("{\"cmd\":\"whois\"}"));
				boost::asio::ip::udp::endpoint remote_endpoint;
				remote_endpoint = boost::asio::ip::udp::endpoint(boost::asio::ip::address::from_string("224.0.0.50"), 4321);
				socket_.send_to(boost::asio::buffer(*message), remote_endpoint);
				socket_.set_option(boost::asio::ip::multicast::join_group(boost::asio::ip::address::from_string("224.0.0.50")));
			}
		}
		catch (const boost::system::system_error& ex) {
			_log.Log(LOG_ERROR, "XiaomiGateway: %s", ex.code().category().name());
			m_XiaomiGateway->StopHardware();
			return;
		}
		start_receive();
	}
	else {
	}
}

XiaomiGateway::xiaomi_udp_server::~xiaomi_udp_server()
{
}

void XiaomiGateway::xiaomi_udp_server::start_receive()
{
	//_log.Log(LOG_STATUS, "start_receive");
	memset(&data_[0], 0, sizeof(data_));
	socket_.async_receive_from(boost::asio::buffer(data_, max_length), remote_endpoint_, boost::bind(&xiaomi_udp_server::handle_receive, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
}

void XiaomiGateway::xiaomi_udp_server::whois(void)
{
	int sec_counter = 0;
	while (1)
	{
		boost::this_thread::sleep(boost::posix_time::seconds(5));

		sec_counter++;
		if (sec_counter % 3 == 0)
		{
			XiaomiGateway * TrueGateway = m_XiaomiGateway->GatewayByIp(remote_endpoint_.address().to_v4().to_string());

			if (!TrueGateway)
			{
				continue;
			}

			if (false == TrueGateway->IsDevInfoInited())
			{
				std::shared_ptr<std::string> message(new std::string("{\"cmd\":\"whois\"}"));
				boost::asio::ip::udp::endpoint remote_endpoint;
				remote_endpoint = boost::asio::ip::udp::endpoint(boost::asio::ip::address::from_string("224.0.0.50"), 4321);
				socket_.send_to(boost::asio::buffer(*message), remote_endpoint);
			}
		}
	}
}

void XiaomiGateway::xiaomi_udp_server::handle_receive(const boost::system::error_code & error, std::size_t bytes_recvd)
{

	if (error && error != boost::asio::error::message_size)
	{
		_log.Log(LOG_ERROR, "XiaomiGateway: error in handle_receive %s", error.message().c_str());
		return;
	}


	XiaomiGateway * TrueGateway = m_XiaomiGateway->GatewayByIp(remote_endpoint_.address().to_v4().to_string());
	if (!TrueGateway)
	{
		_log.Log(LOG_ERROR, "XiaomiGateway: received data from  unregisted gateway ip:%s!", remote_endpoint_.address().to_v4().to_string().c_str());
		start_receive();
		return;
	}

#ifdef _DEBUG
	_log.Log(LOG_STATUS, data_);
#endif
	_log.Log(LOG_STATUS, "XiaomiGateway: recv: %s", data_);


	Json::Value root;
	bool showmessage = true;
	bool ret = ParseJSon(data_, root);
	if ((!ret) || (!root.isObject()))
	{
		_log.Log(LOG_ERROR, "XiaomiGateway: the received data  json format error!");
		start_receive();
		return;
	}

	std::string cmd = root[xiaomi::key_cmd].asString();
	std::string model = root[xiaomi::key_model].asString();
	std::string sid = root[xiaomi::key_sid].asString();
	Json::Value params = root[xiaomi::key_params];


	_log.Log(LOG_STATUS, "XiaomiGateway: cmd  %s received!", cmd.c_str());


	std::string check = m_XiaomiGateway->GetDeviceModel(sid);
	if (check != model && model != "gateway" &&  model != "gateway.v3" && model != "acpartner.v3" && model != "tenbay_gw")
	{
		_log.Log(LOG_ERROR, "XiaomiGateway: sid(%s) and model(%s) not in dev list, please export to DT first!", sid.c_str(), model.c_str());
		start_receive();
		return;
	}

	int unitcode = 1;
	if ((cmd == xiaomi::rsp_read) || (cmd ==xiaomi::rsp_write) || (cmd == xiaomi::rep_report) || (cmd == xiaomi::rep_hbeat))
	{
		if(!params.isArray())
		{
			start_receive();
			_log.Log(LOG_ERROR, "XiaomiGateway: cmd  %s msg format error!", cmd.c_str());
			return;
		}
		_eSwitchType type = STYPE_END;
		std::string name = "Xiaomi Switch";

		/* status */
		std::string status = "";

		bool commit = false;
		bool on = false;
		int level = 0;

		std::string str_battery = "";
		for (int i = 0; i < (int)params.size(); i++)
		{
			if(params[i].isMember("battery_voltage"))
			{
				str_battery = params[i]["battery_voltage"].asString();
			}
		}

		int battery = 255;
		if (str_battery != "" && str_battery != "3600") {
			battery = ((atoi(str_battery.c_str()) - 2200) / 10);
		}

		/* 传感器 */
		if (model == "motion" || model == "sensor_motion.aq2")
		{
			commit = false;
			type = STYPE_Motion;
			name =  (model == "motion")?"Xiaomi Motion Sensor":"Aqara Motion Sensor";
			int ilux = -1;
			for (int i = 0; i < (int)params.size(); i++)
			{
				if(params[i].isMember("motion_status"))
				{
					status = params[i]["motion_status"].asString();
					on = (status ==  "motion")? true : false;
					level = (status ==  "motion")? 10:0;
					commit = true;
				}
				else if(params[i].isMember("lux"))
				{
					ilux = params[i]["lux"].asInt();
				}
				else if (params[i].isMember("illumination"))
				{
					ilux = params[i]["illumination"].asInt();
				}
			}
			if (commit)
			{
				TrueGateway->InsertUpdateSwitch(sid.c_str(), name, on, type, unitcode, level, cmd, "", "", battery);
			}
			if (ilux > 0)
			{
				TrueGateway->InsertUpdateLux(sid.c_str(), name, ilux, battery);
			}
		}
		else if ((model == "magnet") || (model == "sensor_magnet.aq2"))
		{
			commit = false;
			type = STYPE_Contact;
			name = "Xiaomi Door Sensor";
			for (int i = 0; i < (int)params.size(); i++)
			{
				if(params[i].isMember("window_status"))
				{
					status = params[i]["window_status"].asString();
					on = (status ==  "open")?true : false;
					level = (status ==  "open")?10 : 0;
					commit = true;
				}
			}
			if (commit)
			{
				TrueGateway->InsertUpdateSwitch(sid.c_str(), name, on, type, unitcode, level, cmd, "", "", battery);
			}
		}
		else if (model == "sensor_ht" || model == "weather.v1" || model == "weather")
		{
			bool thp = false;
			bool th = false;
			bool t = false;

			if (model == "sensor_ht")
			{
				name = "Xiaomi Temperature/Humidity";
				th = true;
			}
			else if (model == "weather.v1" || model == "weather" )
			{
				name = "Xiaomi Aqara Weather";
				thp = true;
			}

			std::string stemperature = "";
			std::string shumidity = "";
			std::string spressure = "";

			for (int i = 0; i < (int)params.size(); i++)
			{
				if(params[i].isMember("temperature"))
				{
					stemperature = params[i]["temperature"].asString();
				}
				else if (params[i].isMember("humidity"))
				{
					shumidity = params[i]["humidity"].asString();
				}
				else if (params[i].isMember("pressure"))
				{
					spressure = params[i]["pressure"].asString();
				}
			}

			if (thp)
			{
				TrueGateway->InsertUpdateTempHumPressure(sid.c_str(), "Xiaomi TempHumBaro", stemperature, shumidity, spressure, battery);
			}

#if 0
			if ((!stemperature.empty()) && (!shumidity.empty()) && (!spressure.empty()))
			{
				fpressure = static_cast<float>(atof(spressure.c_str())) / 100.0f;
				//Temp+Hum+Baro
				ftemperature = (float)atof(stemperature.c_str()) / 100.0f;
				ihumidity = atoi(shumidity.c_str()) / 100;
				TrueGateway->InsertUpdateTempHumPressure(sid.c_str(), "Xiaomi TempHumBaro", ftemperature, ihumidity, fpressure, battery);
			}
			else if ((!stemperature.empty()) && (!shumidity.empty()))
			{
				//Temp+Hum
				ftemperature = (float)atof(stemperature.c_str()) / 100.0f;
				ihumidity = atoi(shumidity.c_str()) / 100;
				TrueGateway->InsertUpdateTempHum(sid.c_str(), "Xiaomi TempHum", ftemperature, ihumidity, battery);
			}
			else if (stemperature != "") {
				ftemperature = (float)atof(stemperature.c_str()) / 100.0f;
				if (ftemperature < 99) {
					TrueGateway->InsertUpdateTemperature(sid.c_str(), "Xiaomi Temperature", ftemperature, battery);
				}
			}
			else if (shumidity != "") {
				ihumidity = atoi(shumidity.c_str()) / 100;
				if (ihumidity > 1) {
					TrueGateway->InsertUpdateHumidity(sid.c_str(), "Xiaomi Humidity", ihumidity, battery);
				}
			}
			else if (spressure != "") {
				fpressure = static_cast<float>(atof(spressure.c_str())) / 100.0f;
				if (fpressure > 1) {
					TrueGateway->InsertUpdatePressure(sid.c_str(), "Xiaomi Pressure", fpressure, battery);
				}
			}
#endif
		}
		/* 状态型 */
		else if (model == "plug" || model == "plug.maeu01" ||
				model == "86plug" || model == "ctrl_86plug.aq1" ||
				model == "ctrl_neutral1" || model == "ctrl_ln1" || model == "ctrl_ln1.aq1" ||
				model == "ctrl_neutral2" || model == "ctrl_ln2" || model == "ctrl_ln2.aq1")
		{
			commit = false;
			std::string load_power = "";
			std::string  consumed = "";

			type = STYPE_OnOff;
			if (model == "plug" || model == "plug.maeu01")
			{
				name = "Xiaomi Smart Plug";
			}
			else if (model == "86plug" || model == "ctrl_86plug.aq1")
			{
				name = "Xiaomi Smart Wall Plug";
			}
			else if (model == "ctrl_neutral1" || model == "ctrl_ln1" || model == "ctrl_ln1.aq1")
			{
				name = "Xiaomi Wired Single Wall Switch";
			}
			else if (model == "ctrl_neutral2" || model == "ctrl_ln2" || model == "ctrl_ln2.aq1")
			{
				name = "Xiaomi Wired Dual Wall Switch";
			}

			for (int i = 0; i < (int)params.size(); i++)
			{
				if(params[i].isMember("channel_0"))
				{
					status = params[i]["channel_0"].asString();
					on = (status ==  "on")?true : false;
					level = (status ==  "on")?10 : 0;
					commit = true;
					if (model == "ctrl_neutral1" || model == "ctrl_ln1" || model == "ctrl_ln1.aq1")
					{
						unitcode = 1;
						TrueGateway->InsertUpdateSwitch(sid.c_str(), name, on, type, unitcode, level, cmd, load_power, consumed, battery);
						commit = false;
					}
					else if (model == "ctrl_neutral2" || model == "ctrl_ln2" || model == "ctrl_ln2.aq1")
					{
						unitcode = 1;
						name = "Xiaomi Wired Dual Wall Switch Channel 0";
						TrueGateway->InsertUpdateSwitch(sid.c_str(), name, on, type, unitcode, level, cmd, load_power, consumed, battery);
						commit = false;
					}
				}
				else if(params[i].isMember("channel_1"))
				{
					status = params[i]["channel_1"].asString();
					on = (status ==  "on")?true : false;
					level = (status ==  "on")?10 : 0;
					commit = true;
					if (model == "ctrl_neutral2" || model == "ctrl_ln2" || model == "ctrl_ln2.aq1")
					{
						unitcode =  2;
						name = "Xiaomi Wired Dual Wall Switch Channel 1";
						TrueGateway->InsertUpdateSwitch(sid.c_str(), name, on, type, unitcode, level, cmd, load_power, consumed, battery);
						commit = false;
					}
				}
				else if (params[i].isMember("load_power"))
				{
					load_power = params[i]["load_power"].asString();
					//if (atof(load_power.c_str()) > 0.0)
					{
						on= true;
						level = 10;
					}
					commit = true;
				}
				else if (params[i].isMember("energy_consumed"))
				{
					consumed = params[i]["energy_consumed"].asString();
					commit = true;
				}
			}
			if (commit)
			{
				sleep_milliseconds(200);
				TrueGateway->InsertUpdateSwitch(sid.c_str(), name, on, type, unitcode, level, cmd, load_power, consumed, battery);			
			}
		}

		/* 动作型 */
		else if ((model == "switch") || (model == "remote.b1acn01") ||
			model == "sensor_switch.aq2" ||
			model == "sensor_switch.aq3" ||
			model == "86sw1" || model == "remote.b186acn01" || model == "sensor_86sw1.aq1" ||
			model == "86sw2" || model == "remote.b286acn01" || model == "sensor_86sw1.aq1")
		{
			type = STYPE_Selector;
			int level = 0;
			int channel = 0;
			on = false;

			if (model == "switch" || model == "remote.b1acn01")
			{
				name = "Xiaomi Wireless Switch";
			}
			else if (model == "sensor_switch.aq2")
			{
				name = "Xiaomi Square Wireless Switch";
			}
			else if (model == "sensor_switch.aq3")
			{
				name = "Xiaomi Smart Push Button";
			}
			else if(model == "86sw1" || model == "sensor_86sw1.aq1" || model == "remote.b186acn01")
			{
				name = "Xiaomi Wireless Single Wall Switch";

			}
			else if (model == "86sw2" || model == "sensor_86sw2.aq1" || model == "remote.b286acn01")
			{
				name = "Xiaomi Wireless Dual Wall Switch";
			}

			std::string dual_chn = "";
			for (int i = 0; i < (int)params.size(); i++)
			{
				
				if(params[i].isMember("button_0"))
				{
					status = params[i]["button_0"].asString();
				}
				else if(params[i].isMember("button_1"))
				{
					status = params[i]["button_1"].asString();
					channel = 1;
				}
				else if (params[i].isMember("dual_channel"))
				{
					dual_chn = params[i]["dual_channel"].asString();
				}
			}

			if (status == "click")
			{
				level = (channel == 0)? 10:40;
				on = true;
			}
			else if (status == "double_click")
			{
				level = (channel == 0)? 20:50;
				on = true;
			}
			else if (status == "long_click")
			{
				level = (channel == 0)?30:60;
				on = true;
			}
			else if (status == "shake")
			{
				level = 40;
				on = true;
			}
			else if (dual_chn == "click")
			{
				level = 70;
				on = true;
			}
			else if (dual_chn == "double_click")
			{
				level = 80;
				on = true;
			}
			else if (dual_chn == "long_click")
			{
				level = 90;
				on = true;
			}

			if (on)
			{
				TrueGateway->InsertUpdateSwitch(sid.c_str(), name, on, type, unitcode, level, cmd, "", "", battery);
			}
		}
		/* 动作型 */
		else if (model == "cube" || model == "sensor_cube.aqgl01")
		{
			if (model == "cube")
			{
				name = "Xiaomi Cube";
			}
			else if (model == "sensor_cube.aqgl01")
			{
				name = "Aqara Cube";
			}

			type = STYPE_Selector;
			std::string rdegree = "";
			std::string  rtime = "";
			commit = false;

			for (int i = 0; i < (int)params.size(); i++)
			{
				if(params[i].isMember("cube_status"))
				{
					status = params[i]["cube_status"].asString();
					commit = true;
				}
				else if(params[i].isMember("rotate_degree"))
				{
					rdegree = params[i]["rotate_degree"].asString();
					commit = true;
				}
				else if (params[i].isMember("detect_time"))
				{
					rtime = params[i]["detect_time"].asString();
					commit = true;
				}
			}

			if (status == "flip90")
			{
				level = 10;
			}
			else if (status == "flip180")
			{
				level = 20;
			}
			else if (status == "move")
			{
				level = 30;
			}
			else if (status == "tap_twice")
			{
				level = 40;
			}
			else if (status == "shake")
			{
				level = 50;
			}
			else if (status == "swing")
			{
				level = 60;
			}
			else if (status == "alert")
			{
				level = 70;
			}
			else if (status == "free_fall")
			{
				level = 80;
			}
			else if (status == "rotate")
			{
				level = 90;
			}
			if (level > 0)
			{
				on = true;
				//status = status +"  "+ rdegree +"degree in "+ rtime +"ms";
				//TrueGateway->InsertUpdateCubeText(sid.c_str(), name, status.c_str());
				TrueGateway->InsertUpdateSwitch(sid.c_str(), name, on, type, unitcode, level, cmd, "", "", battery);
			}
		}
		/* 动作型 */
		else if (model == "vibration" || model == "vibration.aq1")
		{
			std::string angle = "";
			name = "Aqara Vibration Sensor";
			type = STYPE_Selector;
			for (int i = 0; i < (int)params.size(); i++)
			{
				if(params[i].isMember("status"))
				{
					status = params[i]["status"].asString();
					commit = true;
				}
				if(params[i].isMember("angle"))
				{
					angle = params[i]["angle"].asString();
					commit = true;
				}
			}
			if (status == "touch")
			{
				level = 10;
				on = true;
			}
			else if (status == "tilt")
			{
				level = 20;
				on = true;
			}
			else if (status == "drop")
			{
				level = 30;
				on = true;
			}
			else if (angle != "")
			{
				level = 40;
				on = true;
			}

			if (angle != "")
			{
				status = "angle : " + angle;
			}
			if (commit)
			{
				//TrueGateway->InsertUpdateCubeText(sid.c_str(), name, status.c_str());
				TrueGateway->InsertUpdateSwitch(sid.c_str(), name, on, type, unitcode, level, cmd, "", "", battery);
			}
		}
		else if (model == "smoke" || model == "natgas" ||
				model == "sensor_wleak.aq1")
		{

			if (model == "smoke")
			{
				name = "Xiaomi Smoke Detector";
			}
			else if (model == "natgas")
			{
				name = "Xiaomi Gas Detector";
			}
			else if (model == "sensor_wleak.aq1")
			{
				name = "Xiaomi Water Leak Detector";
			}

			type = STYPE_SMOKEDETECTOR;

			for (int i = 0; i < (int)params.size(); i++)
			{
				if(params[i].isMember("wleak_status"))
				{
					status = params[i]["wleak_status"].asString();
					commit = true;
				}
				else if(params[i].isMember("natgas_status"))
				{
					status = params[i]["natgas_status"].asString();
					commit = true;
				}
				else if (params[i].isMember("smoke_status"))
				{
					status = params[i]["smoke_status"].asString();
					commit = true;
				}
			}
			if (status == "leak")
			{
				unitcode = 1;
				level = 10;
				on = true;
			}

			if (commit)
			{
				TrueGateway->InsertUpdateSwitch(sid.c_str(), name, on, type, unitcode, level, cmd, "", "", battery);
			}

		}
		else if (model == "curtain")
		{
			name = "Xiaomi Curtain";
			type = STYPE_BlindsPercentage;

			on = false;
			std::string curtain_level = "";
			for (int i = 0; i < (int)params.size(); i++)
			{
				if(params[i].isMember("curtain_status"))
				{
					status = params[i]["curtain_status"].asString();
					commit = true;
				}
				else if(params[i].isMember("curtain_level"))
				{
					curtain_level = params[i]["curtain_level"].asString();
					commit = true;
				}
			}

			if(status == "open" )
			{
				on = true;
			}
			if (commit)
			{
				level = atoi(curtain_level.c_str());
				TrueGateway->InsertUpdateSwitch(sid.c_str(), name, on, type, unitcode, level, cmd, "", "", battery);
			}

		}
		else if (model == "dimmer.rgbegl01" || model == "light.aqcn02")
		{
			name = "Xiaomi RGB Light";

			std::string light_level = "";
			std::string light_rgb = "";
			std::string color_temp = "";
			static unsigned int oldbrightness = 0;
			unsigned int irgb = 0;
			unsigned int ib = 0;
			unsigned int it = 0;

			bool brgb = false;
			bool bb = false;
			bool bt = false;
			int action = -1;

			for (int i = 0; i < (int)params.size(); i++)
			{
				if(params[i].isMember("power_status"))
				{
					status = params[i]["power_status"].asString();
				}
				else if(params[i].isMember("light_rgb"))
				{
					light_rgb = params[i]["light_rgb"].asString();
					brgb = true;
				}
				else if(params[i].isMember("light_level"))
				{
					light_level  = params[i]["light_level"].asString();
					bb = true;
				}
				else if(params[i].isMember("color_temp"))
				{
					color_temp = params[i]["color_temp"].asString();
					bt = true;
				}
			}
			if(status == "on")
			{
			}
			if (light_rgb != "")
			{
				std::cout<<"--------bbbrgb----"<<std::endl;
			}

			if (light_level != "")
			{
				std::cout<<"--------bb----"<<std::endl;
				int ib = 0;
				ib = atoi(light_level.c_str());
				ib = ib*0x64 /0xff;
				light_level = std::to_string(ib);
			}

			if (light_rgb != "" || light_level != "")
			{
				TrueGateway->InsertUpdateRGBLight(sid, name, sTypeColor_CW_WW, ColorModeRGB, light_rgb, light_level, false, battery);
			}
			_log.Log(LOG_STATUS, "led: rgb:%s   brightness_hex:%s", light_rgb.c_str(), light_level.c_str());

		}
		else if (model == "gateway" || model == "gateway.v3" || model == "acpartner.v3" || model == "tenbay_gw")
		{
			name = "Xiaomi RGB Gateway";
			if (model == "tenbay_gw")
			{
				name = "Tenbay Gateway";
			}

			//check for token
			std::string token = root["token"].asString();
			std::string ip = "";
			std::string jb = "";
			for (int i = 0; i < (int)params.size(); i++)
			{
				if(params[i].isMember("ip"))
				{
					ip = params[i]["ip"].asString();
				}
				if(params[i].isMember("JoinButton"))
				{
					jb = params[i]["JoinButton"].asString();
				}
			}
			if ((token != "") && (ip != "")) {
				XiaomiGatewayTokenManager::GetInstance().UpdateTokenSID(ip, token, sid);
				showmessage = false;
			}

			if (jb != "")
			{
				bool ison = (jb == "on")?true:false;
				TrueGateway->InsertUpdateSwitch(sid.c_str(), "Xiaomi Gateway Join Button", ison, STYPE_OnOff, 254, 0, cmd, "", "", 255);
			}


			if (model != "tenbay_gw")
			{
				std::string srgb = "";
				for (int i = 0; i < (int)params.size(); i++)
				{
					if(params[i].isMember("argb_value"))
					{
						srgb = params[i]["argb_value"].asString();
					}
				}

				if (srgb != "")
				{
					// Only add in the gateway that matches the SID for this hardware.
					if (TrueGateway->GetGatewaySid() == sid)
					{
						std::stringstream ss;
						ss << std::hex << atoi(srgb.c_str());
						std::string hexstring(ss.str());
						if (hexstring.length() == 7) {
							hexstring.insert(0, "0");
						}
						std::string bright_hex = hexstring.substr(0, 2);
						std::stringstream ss2;
						ss2 << std::hex << bright_hex.c_str();
						int brightness = strtoul(bright_hex.c_str(), NULL, 16);
						bool on = false;
						if (srgb != "0") {
							on = true;
						}
						TrueGateway->InsertUpdateRGBGateway(sid.c_str(), name + " (" + TrueGateway->GetGatewayIp() + ")", on, brightness, 0);
						TrueGateway->InsertUpdateSwitch(sid.c_str(), "Xiaomi Gateway Alarm Ringtone", false, STYPE_Selector, 3, 0, cmd, "", "", 255);
						TrueGateway->InsertUpdateSwitch(sid.c_str(), "Xiaomi Gateway Alarm Clock", false, STYPE_Selector, 4, 0, cmd, "", "", 255);
						TrueGateway->InsertUpdateSwitch(sid.c_str(), "Xiaomi Gateway Doorbell", false, STYPE_Selector, 5, 0, cmd, "", "", 255);
						TrueGateway->InsertUpdateSwitch(sid.c_str(), "Xiaomi Gateway MP3", false, STYPE_OnOff, 6, 0, cmd, "", "", 255);
						TrueGateway->InsertUpdateSwitch(sid.c_str(), "Xiaomi Gateway Volume", false, STYPE_Dimmer, 7, 0, cmd, "", "", 255);
					}
				}
			}
		}
		else
		{
			_log.Log(LOG_STATUS, "XiaomiGateway (ID=%d): unhandled model: %s, name: %s", TrueGateway->GetGatewayHardwareID(), model.c_str(), name.c_str());
		}

#if 0				
		if (str_battery != "" && name != "" && sid != "" && m_IncludeVoltage) {
			TrueGateway->InsertUpdateVoltage(sid.c_str(), name, atoi(str_battery.c_str()));
		}
#endif
		_log.Log(LOG_STATUS,  "type:%d name:%s", type, name.c_str());
	}
	else if (cmd == xiaomi::rsp_discorey)
	{
		Json::Value list = root["dev_list"];
		std::string sid = "";
		std::string token = "";
		std::string model = "";
		std::string name = "";
		std::string soptions = "";
		std::string devopt = "";
		int devtype = 0;
		int subtype = 0;
		int switchtype = 0;
		int ssid = 0;
		int uint = 0;
		uint64_t DeviceRowIdx = (uint64_t )-1;


		sid = root["sid"].asString();
		token = root["token"].asString();
		std::string ip = remote_endpoint_.address().to_v4().to_string();

		XiaomiGatewayTokenManager::GetInstance().UpdateTokenSID(ip, token, sid);
		/* get device list , set up ssid model mac map */
		TrueGateway->SetDevInfoInited(true);

		for (int i = 0; i < (int)list.size(); i++)
		{
			sid = list[i]["sid"].asString();
			model = list[i]["model"].asString();
			if (sid == "" || model == ""){
				continue;
			}
			
			std::string message = "{\"cmd\" : \"read\",\"sid\":\"";
			message.append(sid.c_str());
			message.append("\"}");
			std::shared_ptr<std::string> message1(new std::string(message));
			boost::asio::ip::udp::endpoint remote_endpoint;
			remote_endpoint = boost::asio::ip::udp::endpoint(boost::asio::ip::address::from_string(TrueGateway->GetGatewayIp().c_str()), m_uincastport);
			socket_.send_to(boost::asio::buffer(*message1), remote_endpoint);

			_log.Log(LOG_STATUS,  "send to GW :%s", message.c_str());

			TrueGateway->SetDeviceInfo(sid, model);
		}

		TrueGateway->CreateNewDevice();
		showmessage = false;
	}
	else if (cmd == xiaomi::rsp_whois)
	{
		if (!root.isMember("ip") || !root.isMember("port"))
		{
			_log.Log(LOG_ERROR, "%s: iam message format error", model.c_str());
			start_receive();
			return;
		}
		std::string ip = root["ip"].asString();
		std::string port =  root["port"].asString();
		m_uincastport = (port == "")? 9494 : atoi(port.c_str());
		TrueGateway->SetUincastPort(m_uincastport);

		if (model == "gateway" || model == "gateway.v3" || model == "acpartner.v3" || model == "gateway.aq1")
		{
			if (ip == TrueGateway->GetGatewayIp())
			{
				_log.Log(LOG_STATUS, "XiaomiGateway: RGB Gateway Detected");

				TrueGateway->InsertUpdateRGBGateway(sid.c_str(), "Xiaomi RGB Gateway (" + ip + ")", false, 0, 100);
				TrueGateway->InsertUpdateSwitch(sid.c_str(), "Xiaomi Gateway Alarm Ringtone", false, STYPE_Selector, 3, 0, cmd, "", "", 255);
				TrueGateway->InsertUpdateSwitch(sid.c_str(), "Xiaomi Gateway Alarm Clock", false, STYPE_Selector, 4, 0, cmd, "", "", 255);
				TrueGateway->InsertUpdateSwitch(sid.c_str(), "Xiaomi Gateway Doorbell", false, STYPE_Selector, 5, 0, cmd, "", "", 255);
				TrueGateway->InsertUpdateSwitch(sid.c_str(), "Xiaomi Gateway MP3", false, STYPE_OnOff, 6, 0, cmd, "", "", 255);
				TrueGateway->InsertUpdateSwitch(sid.c_str(), "Xiaomi Gateway Volume", false, STYPE_Dimmer, 7, 0, cmd, "", "", 255);
				TrueGateway->InsertUpdateSwitch(sid.c_str(), "Xiaomi Gateway Join Button", false, STYPE_OnOff, 254, 0, cmd, "", "", 255);

				//query for list of devices
				std::string message = "{\"cmd\" : \"discovery\"}";
				std::shared_ptr<std::string> message2(new std::string(message));
				boost::asio::ip::udp::endpoint remote_endpoint;
				remote_endpoint = boost::asio::ip::udp::endpoint(boost::asio::ip::address::from_string(TrueGateway->GetGatewayIp().c_str()), m_uincastport);
				socket_.send_to(boost::asio::buffer(*message2), remote_endpoint);
			}
		}
		else if (model == "tenbay_gw")
		{
			if (ip == TrueGateway->GetGatewayIp())
			{
				_log.Log(LOG_STATUS, "Tenbat Gateway: RGB Gateway Detected");

				TrueGateway->InsertUpdateSwitch(sid.c_str(), "Gateway Join Button", false, STYPE_OnOff, 254, 0, cmd, "", "", 255);

				std::string message = "{\"cmd\" : \"discovery\"}";
				std::shared_ptr<std::string> message2(new std::string(message));
				boost::asio::ip::udp::endpoint remote_endpoint;
				remote_endpoint = boost::asio::ip::udp::endpoint(boost::asio::ip::address::from_string(TrueGateway->GetGatewayIp().c_str()), m_uincastport);
				socket_.send_to(boost::asio::buffer(*message2), remote_endpoint);
			}
		}
		showmessage = false;
	}
	else
	{
		_log.Log(LOG_STATUS, "XiaomiGateway (ID=%d): unknown cmd received: %s, model: %s", TrueGateway->GetGatewayHardwareID(), cmd.c_str(), model.c_str());
	}

	
	if (showmessage && m_OutputMessage) {
		_log.Log(LOG_STATUS, "%s", data_);
	}
	start_receive();
}


XiaomiGateway::XiaomiGatewayTokenManager& XiaomiGateway::XiaomiGatewayTokenManager::GetInstance()
{
	static XiaomiGateway::XiaomiGatewayTokenManager instance;
	return instance;
}

void XiaomiGateway::XiaomiGatewayTokenManager::UpdateTokenSID(const std::string & ip, const std::string & token, const std::string & sid)
{
	bool found = false;
	std::unique_lock<std::mutex> lock(m_mutex);
	for (unsigned i = 0; i < m_GatewayTokens.size(); i++) {
		if (boost::get<0>(m_GatewayTokens[i]) == ip) {
			boost::get<1>(m_GatewayTokens[i]) = token;
			boost::get<2>(m_GatewayTokens[i]) = sid;
			found = true;
		}
	}
	if (!found) {
		m_GatewayTokens.push_back(boost::make_tuple(ip, token, sid));
	}

}

std::string XiaomiGateway::XiaomiGatewayTokenManager::GetToken(const std::string & ip)
{
	std::string token = "";
	bool found = false;
	std::unique_lock<std::mutex> lock(m_mutex);
	for (unsigned i = 0; i < m_GatewayTokens.size(); i++) {
		if (boost::get<0>(m_GatewayTokens[i]) == ip) {
			token = boost::get<1>(m_GatewayTokens[i]);
		}
	}
	return token;
}

std::string XiaomiGateway::XiaomiGatewayTokenManager::GetSID(const std::string & ip)
{
	std::string sid = "";
	bool found = false;
	std::unique_lock<std::mutex> lock(m_mutex);
	for (unsigned i = 0; i < m_GatewayTokens.size(); i++) {
		if (boost::get<0>(m_GatewayTokens[i]) == ip) {
			sid = boost::get<2>(m_GatewayTokens[i]);
		}
	}
	return sid;
}


namespace http {
	namespace server {

		void CWebServer::Cmd_AddZigbeeDevice(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (session.rights != 2)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}
			std::string hwid = request::findValue(&req, "idx");
			std::string model = request::findValue(&req, "model");

			if (hwid == "")
				return;
			int iHardwareID = atoi(hwid.c_str());
			CDomoticzHardwareBase *pHardware = m_mainworker.GetHardware(iHardwareID);
			if (pHardware == NULL)
				return;
			if (pHardware->HwdType != HTYPE_XiaomiGateway)
				return;

			root["status"] = "OK";
			root["title"] = "AddZigbeeDevice";
			m_mainworker.AddZigbeeDevice(hwid, model);
		}

		}
}

