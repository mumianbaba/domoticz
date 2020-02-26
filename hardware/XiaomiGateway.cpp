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
			_log.Log(LOG_ERROR, "XiaomiGateway: by ip 	yp %s  %s", (*it)->GetGatewayIp().c_str(), ip.c_str());
			if ((*it)->GetGatewayIp() == ip)
			{
				ret = (*it);
				break;
			};
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
			};
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

XiaomiGateway::XiaomiGateway(const int ID)
{
	m_HwdID = ID;
	m_bDoRestart = false;
	m_ListenPort9898 = false;
}

XiaomiGateway::~XiaomiGateway(void)
{
}

bool XiaomiGateway::WriteToHardware(const char * pdata, const unsigned char length)
{
	const tRBUF *pCmd = reinterpret_cast<const tRBUF *>(pdata);
	unsigned char packettype = pCmd->ICMND.packettype;
	unsigned char subtype = pCmd->ICMND.subtype;
	bool result = true;
	std::string message = "";

	if (m_GatewaySID == "") {
		m_GatewaySID = XiaomiGatewayTokenManager::GetInstance().GetSID(m_GatewayIp);
	}

	char szTmp[50];
	std::string sid = "";
	std::string ID = "";
	std::stringstream s_strid2;

	if (packettype == pTypeGeneralSwitch) {
		_tGeneralSwitch *xcmd = (_tGeneralSwitch*)pdata;

		sprintf(szTmp, "%08X", (unsigned int)xcmd->id);
		ID = szTmp;
		s_strid2 << std::hex << ID;
		sid = s_strid2.str();
		std::transform(sid.begin(), sid.end(), sid.begin(), ::tolower);
		std::string cmdchannel = "";
		std::string cmdcommand = "";
		std::string cmddevice = "";
		//bool isctrl2 = false;
		std::string sidtemp = sid;
		sidtemp.insert(0, "158d00");

		if (xcmd->unitcode == 8) {
			cmdchannel = "\"channel_0\":";
			cmddevice = "ctrl_neutral1";
		}
		else if (xcmd->unitcode == 9) {
			cmdchannel = "\"channel_0\":";
			cmddevice = "ctrl_neutral2";
		}
		else if (xcmd->unitcode == 10) {
			cmdchannel = "\"channel_1\":";
			cmddevice = "ctrl_neutral2";
		}
		if (xcmd->cmnd == 0) {
			cmdcommand = "\"off\"";
		}
		else if (xcmd->cmnd == 1) {
			cmdcommand = "\"on\"";
		}

		if (xcmd->unitcode == 8 || xcmd->unitcode == 9 || xcmd->unitcode == 10) {
			message = "{\"cmd\":\"write\",\"model\":\"" + cmddevice + "\",\"sid\":\"158d00" + sid + "\",\"key\":\"@gatewaykey\",\"params\":[{" + cmdchannel + cmdcommand + "}]}";
		}
		else if ((xcmd->subtype == sSwitchGeneralSwitch) && (xcmd->unitcode == 1)) {
			std::string command = "on";
			switch (xcmd->cmnd) {
			case gswitch_sOff:
				command = "off";
				break;
			case gswitch_sOn:
				command = "on";
				break;
			default:
				_log.Log(LOG_ERROR, "XiaomiGateway: Unknown command %d", xcmd->cmnd);
				break;
			}
			message = "{\"cmd\":\"write\",\"model\":\"plug\",\"sid\":\"158d00" + sid + "\",\"key\":\"@gatewaykey\",\"params\":[{\"channel_0\":\"" +command+ "\"}]}";
		}
		else if ((xcmd->subtype == sSwitchTypeSelector) && (xcmd->unitcode >= 3 && xcmd->unitcode <= 5) || (xcmd->subtype == sSwitchGeneralSwitch) && (xcmd->unitcode == 6)) {
			std::stringstream ss;
			if (xcmd->unitcode == 6) {
				if (xcmd->cmnd == 1) {
					std::vector<std::vector<std::string> > result;
					result = m_sql.safe_query("SELECT Value FROM UserVariables WHERE (Name == 'XiaomiMP3')");
					ss << result[0][0];
				}
				else {
					ss << "10000";
				}
			}
			else {
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
				ss << level;
			}
			m_GatewayMusicId = ss.str();
			//sid.insert(0, m_GatewayPrefix);
			message = "{\"cmd\":\"write\",\"model\":\"gateway\",\"sid\":\"" + m_GatewaySID + "\",\"key\":\"@gatewaykey\",\"params\":[{\"mid\":"+ m_GatewayMusicId.c_str() + "},{\"vol\":" + m_GatewayVolume.c_str() + "}]}";			

		}
		else if (xcmd->subtype == sSwitchGeneralSwitch && xcmd->unitcode == 7) {
			//Xiaomi Gateway volume control
			m_GatewayVolume = std::to_string(xcmd->level);
			//sid.insert(0, m_GatewayPrefix);
			message = "{\"cmd\":\"write\",\"model\":\"gateway\",\"sid\":\"" + m_GatewaySID + "\",\"key\":\"@gatewaykey\",\"params\":[{\"mid\":"+ m_GatewayMusicId.c_str() + "},{\"vol\":" + m_GatewayVolume.c_str() + "}]}";
		}
		else if (xcmd->subtype == sSwitchGeneralSwitch && xcmd->unitcode == 254){
			message = "{\"cmd\":\"write\",\"model\":\"plug\",\"sid\":\"158d00" + sid + "\",\"key\":\"@gatewaykey\",\"params\":[{\"join_permission\":\"yes\"}]}";
		}
		else if (xcmd->subtype == sSwitchBlindsT2) {
			int level = xcmd->level;
			if (xcmd->cmnd == 1) {
				level = 100;
			}
			message = "{\"cmd\":\"write\",\"model\":\"curtain\",\"sid\":\"158d00" + sid + "\",\"key\":\"@gatewaykey\",\"params\":[{\"curtain_level\":" +std::to_string(level) + "}] }";
		}
	}
	else if (packettype == pTypeColorSwitch) {
		//Gateway RGB Controller
		const _tColorSwitch *xcmd = reinterpret_cast<const _tColorSwitch*>(pdata);
		std::stringstream ss;

		sprintf(szTmp, "%08X", (unsigned int)xcmd->id);
		ID = szTmp;
		s_strid2 << std::hex << ID;
		sid = s_strid2.str();
		std::transform(sid.begin(), sid.end(), sid.begin(), ::tolower);

		if (xcmd->command == Color_LedOn){
			m_GatewayBrightnessInt = 42;

			m_GatewayRgbR = 0xff;
			m_GatewayRgbG = 0xff;
			m_GatewayRgbB = 0xff;
			m_GatewayRgbCW = 0;
			m_GatewayRgbWW = 0;
			m_GatewayRgbCT = 0;
			uint32_t value = (m_GatewayBrightnessInt << 24) | (m_GatewayRgbR << 16) | (m_GatewayRgbG << 8) | (m_GatewayRgbB);
			uint32_t cwww = m_GatewayRgbCW << 8 | m_GatewayRgbWW;
			ss << "{\"cmd\":\"write\",\"model\":\"gateway\",\"sid\":\"158d00" << sid << "\",\"key\":\"@gatewaykey\", \"params\":[{\"light_rgb\":"<<value<<"},{\"light_cwww\":"<<cwww<<"},{\"light_ct\":"<<(int)m_GatewayRgbCT<<"}]}";
			message = ss.str();
		}
		else if (xcmd->command == Color_LedOff) {
			m_GatewayBrightnessInt = 0;
			ss << "{\"cmd\":\"write\",\"model\":\"gateway\",\"sid\":\"158d00" << sid << "\",\"key\":\"@gatewaykey\", \"params\":[{\"light_rgb\": 0}]}";
			message = ss.str();
		}
		else if (xcmd->command == Color_SetColor) {

			if (xcmd->color.mode == ColorModeWhite)
			{
				m_GatewayRgbR = 0xff;
				m_GatewayRgbG = 0xff;
				m_GatewayRgbB = 0xff;
				m_GatewayBrightnessInt = xcmd->value;
				uint32_t value = (m_GatewayBrightnessInt << 24) | (m_GatewayRgbR << 16) | (m_GatewayRgbG << 8) | (m_GatewayRgbB);
				ss << "{\"cmd\":\"write\",\"model\":\"gateway\",\"sid\":\"158d00" << sid << "\",\"key\":\"@gatewaykey\", \"params\":[{\"light_rgb\":"<<value<<"}]}";
				message = ss.str();

			}
			else if (xcmd->color.mode == ColorModeTemp)
			{
				m_GatewayRgbCW = xcmd->color.cw;
				m_GatewayRgbWW = xcmd->color.ww;
				m_GatewayRgbCT = xcmd->color.t;
				uint32_t cwww = m_GatewayRgbCW << 8 | m_GatewayRgbWW;
				ss << "{\"cmd\":\"write\",\"model\":\"gateway\",\"sid\":\"158d00" << sid << "\",\"key\":\"@gatewaykey\", \"params\":[{\"light_cwww\":"<<cwww<<"}, {\"light_temp\":"<<(int)m_GatewayRgbCT<<"}]}";
				message = ss.str();
			}
			else if (xcmd->color.mode == ColorModeRGB)
			{
				m_GatewayRgbR = xcmd->color.r;
				m_GatewayRgbG = xcmd->color.g;
				m_GatewayRgbB = xcmd->color.b;
				m_GatewayBrightnessInt = xcmd->value; //TODO: What is the valid range for XiaomiGateway, 0..100 or 0..255?

				uint32_t value = (m_GatewayBrightnessInt << 24) | (m_GatewayRgbR << 16) | (m_GatewayRgbG << 8) | (m_GatewayRgbB);

				ss << "{\"cmd\":\"write\",\"model\":\"gateway\",\"sid\":\"158d00" << sid << "\",\"key\":\"@gatewaykey\", \"params\":[{\"light_rgb\":"<<value<<"}]}";
				message = ss.str();
			}
			else if (xcmd->color.mode == ColorModeCustom)
			{
				m_GatewayRgbR = xcmd->color.r;
				m_GatewayRgbG = xcmd->color.g;
				m_GatewayRgbB = xcmd->color.b;
				m_GatewayRgbCW = xcmd->color.cw;
				m_GatewayRgbWW = xcmd->color.ww;
				m_GatewayRgbCT = xcmd->color.t;
				m_GatewayBrightnessInt = xcmd->value; //TODO: What is the valid range for XiaomiGateway, 0..100 or 0..255?

				uint32_t value = (m_GatewayBrightnessInt << 24) | (m_GatewayRgbR << 16) | (m_GatewayRgbG << 8) | (m_GatewayRgbB);
				uint32_t cwww = m_GatewayRgbCW << 8 | m_GatewayRgbWW;
				ss << "{\"cmd\":\"write\",\"model\":\"gateway\",\"sid\":\"158d00" << sid << "\",\"key\":\"@gatewaykey\", \"params\":[{\"light_rgb\":"<<value<<"},{\"light_cwww\":"<<cwww<<"},{\"light_ct\":"<<(int)m_GatewayRgbCT<<"}]}";
				message = ss.str();
			}
			else
			{
				_log.Log(LOG_STATUS, "XiaomiGateway: Set_Colour - Color mode %d is unhandled, if you have a suggestion for what it should do, please post on the Domoticz forum", xcmd->color.mode);
			}
		}
		else if ((xcmd->command == Color_SetBrightnessLevel) || (xcmd->command == Color_SetBrightUp) || (xcmd->command == Color_SetBrightDown)) {
			//add the brightness
			if (xcmd->command == Color_SetBrightUp) {
				//m_GatewayBrightnessInt = std::min(m_GatewayBrightnessInt + 10, 100);
			}
			else if (xcmd->command == Color_SetBrightDown) {
				//m_GatewayBrightnessInt = std::max(m_GatewayBrightnessInt - 10, 0);
			}
			else {
				m_GatewayBrightnessInt = (int)xcmd->value; //TODO: What is the valid range for XiaomiGateway, 0..100 or 0..255?
			}

			uint32_t value = (m_GatewayBrightnessInt << 24) | (m_GatewayRgbR << 16) | (m_GatewayRgbG << 8) | (m_GatewayRgbB);

			std::stringstream ss;
			ss << "{\"cmd\":\"write\",\"model\":\"gateway\",\"sid\":\"" << m_GatewaySID << "\",\"short_id\":0,\"data\":\"{\\\"rgb\\\":" << value << ",\\\"key\\\":\\\"@gatewaykey\\\"}\" }";
			message = ss.str();
		}
		else if (xcmd->command == Color_SetColorToWhite) {
			//ignore Color_SetColorToWhite
		}
		else {
			_log.Log(LOG_ERROR, "XiaomiGateway: Unknown command %d", xcmd->command);
		}
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
	remote_endpoint_ = boost::asio::ip::udp::endpoint(boost::asio::ip::address::from_string(m_GatewayIp), 9494);
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
		SendPressureSensor(sID, 1, battery, Pressure, Name);
	}
}

void XiaomiGateway::InsertUpdateTempHumPressure(const std::string &nodeid, const std::string &Name, const float Temperature, const int Humidity, const float Pressure, const int battery)
{
	unsigned int sID = GetShortID(nodeid);
	int barometric_forcast = baroForecastNoInfo;
	if (Pressure < 1000)
		barometric_forcast = baroForecastRain;
	else if (Pressure < 1020)
		barometric_forcast = baroForecastCloudy;
	else if (Pressure < 1030)
		barometric_forcast = baroForecastPartlyCloudy;
	else
		barometric_forcast = baroForecastSunny;

	if (sID > 0) {
		SendTempHumBaroSensor(sID, battery, Temperature, Humidity, Pressure, barometric_forcast, Name);
	}
}

void XiaomiGateway::InsertUpdateTempHum(const std::string &nodeid, const std::string &Name, const float Temperature, const int Humidity, const int battery)
{
	unsigned int sID = GetShortID(nodeid);
	if (sID > 0) {
		SendTempHumSensor(sID, battery, Temperature, Humidity, Name);
	}
}

void XiaomiGateway::InsertUpdateRGBLight(const std::string & nodeid, const std::string & Name, const unsigned char SubType, const std::string& Hex, const std::string& Brightness, const bool bIsWhite, const int battery)
{
	unsigned int sID = GetShortID(nodeid);
	if (sID > 0)
	{
		SendRGBWSwitch(sID, 1, SubType, Hex, Brightness, bIsWhite, battery, Name);
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
		//int value = atoi(brightness.c_str());
		//int value = hue; // atoi(hue.c_str());
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
		//int value = atoi(brightness.c_str());
		if ((bIsOn != tIsOn) || (brightness != lastLevel))
		{
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
		/*if (isctlr2 == true) {
			m_sql.safe_query("UPDATE DeviceStatus SET Name='Xiaomi Wired Switch 1', SwitchType=%d, CustomImage=%i, Unit='1' WHERE(HardwareID == %d) AND (DeviceID == '%q') AND (Unit == '1')", (switchtype), customimage, m_HwdID, ID.c_str());
			xcmd.unitcode = 2;
			m_mainworker.PushAndWaitRxMessage(this, (const unsigned char *)&xcmd, NULL, -1);
			m_sql.safe_query("UPDATE DeviceStatus SET Name='Xiaomi Wired Switch 2', SwitchType=%d, CustomImage=%i, Unit='2' WHERE(HardwareID == %d) AND (DeviceID == '%q') AND (Unit == '2')", (switchtype), customimage, m_HwdID, ID.c_str());
		}
		else {*/
		m_sql.safe_query("UPDATE DeviceStatus SET Name='%q', SwitchType=%d, CustomImage=%i WHERE(HardwareID == %d) AND (DeviceID == '%q') AND (Unit == '%d')", Name.c_str(), (switchtype), customimage, m_HwdID, ID.c_str(), xcmd.unitcode);
		//}

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
					// flip90/flip180/move/tap_twice/shake_air/swing/alert/free_fall
					m_sql.SetDeviceOptions(atoi(Idx.c_str()), m_sql.BuildDeviceOptions("SelectorStyle:0;LevelNames:Off|flip90|flip180|move|tap_twice|shake_air|swing|alert|free_fall|clock_wise|anti_clock_wise", false));
				}
				else if (Name == "Aqara Cube") {
					// flip90/flip180/move/tap_twice/shake_air/swing/alert/free_fall/rotate
					m_sql.SetDeviceOptions(atoi(Idx.c_str()), m_sql.BuildDeviceOptions("SelectorStyle:0;LevelNames:Off|flip90|flip180|move|tap_twice|shake_air|swing|alert|free_fall|rotate", false));
				}
				else if (Name == "Aqara Vibration Sensor") {
					// tilt/vibrate/free fall
					m_sql.SetDeviceOptions(atoi(Idx.c_str()), m_sql.BuildDeviceOptions("SelectorStyle:0;LevelNames:Off|Tilt|Vibrate|Free Fall", false));
				}
				else if (Name == "Xiaomi Wireless Dual Wall Switch") {
					//for Aqara wireless switch, 2 buttons support
					m_sql.SetDeviceOptions(atoi(Idx.c_str()), m_sql.BuildDeviceOptions("SelectorStyle:0;LevelNames:Off|Switch 1|Switch 1 Double Click|Switch 2|Switch 2 Double Click|Both Click", false));
				}
				else if (Name == "Xiaomi Wired Single Wall Switch") {
					//for Aqara wired switch, single button support
					m_sql.SetDeviceOptions(atoi(Idx.c_str()), m_sql.BuildDeviceOptions("SelectorStyle:0;LevelNames:Off|Switch1 On|Switch1 Off", false));
				}
				else if (Name == "Xiaomi Wireless Single Wall Switch") {
					//for Aqara wireless switch, single button support
					m_sql.SetDeviceOptions(atoi(Idx.c_str()), m_sql.BuildDeviceOptions("SelectorStyle:0;LevelNames:Off|Click|Double Click", false));
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
	}
	else {
		/*if (is2ndchannel) {
			xcmd.unitcode = 2;
		}*/
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
			if (load_power != "" && power_consumed != "") {
				int power = atoi(load_power.c_str());
				int consumed = atoi(power_consumed.c_str()) / 1000;
				SendKwhMeter(sID, 1, 255, power, consumed, "Xiaomi Smart Plug Usage");
			}
		}
	}
}

void XiaomiGateway::InsertUpdateCubeText(const std::string & nodeid, const std::string & Name, const std::string &degrees)
{
	unsigned int sID = GetShortID(nodeid);
	if (sID > 0) {
		SendTextSensor(sID, sID, 255, degrees.c_str(), Name);
	}
}

void XiaomiGateway::InsertUpdateVoltage(const std::string & nodeid, const std::string & Name, const int VoltageLevel)
{
	if (VoltageLevel < 3600) {
		unsigned int sID = GetShortID(nodeid);
		if (sID > 0) {
			int percent = ((VoltageLevel - 2200) / 10);
			float voltage = (float)VoltageLevel / 1000;
			SendVoltageSensor(sID, sID, percent, voltage, "Xiaomi Voltage");
		}
	}
}

void XiaomiGateway::InsertUpdateLux(const std::string & nodeid, const std::string & Name, const int Illumination, const int battery)
{
	unsigned int sID = GetShortID(nodeid);
	if (sID > 0) {
		float lux = (float)Illumination;
		SendLuxSensor(sID, sID, battery, lux, Name);
	}
}

bool XiaomiGateway::StartHardware()
{
	RequestStart();

	m_bDoRestart = false;

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
	//m_GatewayPrefix = "f0b4";
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
	if (m_ListenPort9898) {
		bt = boost::thread(boost::bind(&boost::asio::io_service::run, &io_service));
		SetThreadName(bt.native_handle(), "XiaomiGatewayIO");
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
	std::string str;
	if (nodeid.length() < 14) {
		//gateway
		str = nodeid.substr(4, 8);
	}
	else {
		//device
		str = nodeid.substr(6, 8);
	}
	unsigned int sID;
	std::stringstream ss;
	ss << std::hex << str.c_str();
	ss >> sID;
	return sID;
}

XiaomiGateway::xiaomi_udp_server::xiaomi_udp_server(boost::asio::io_service& io_service, int m_HwdID, const std::string &gatewayIp, const std::string &localIp, const bool listenPort9898, const bool outputMessage, const bool includeVoltage, XiaomiGateway *parent)
	: socket_(io_service, boost::asio::ip::udp::v4())
{
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

void XiaomiGateway::xiaomi_udp_server::handle_receive(const boost::system::error_code & error, std::size_t bytes_recvd)
{
	if (!error || error == boost::asio::error::message_size)
	{
		XiaomiGateway * TrueGateway = m_XiaomiGateway->GatewayByIp(remote_endpoint_.address().to_v4().to_string());

		if (!TrueGateway)
		{
			_log.Log(LOG_ERROR, "XiaomiGateway: received data from  unregisted gateway!");
			start_receive();
			return;
		}
#ifdef _DEBUG
		_log.Log(LOG_STATUS, data_);
#endif
		Json::Value root;
		bool showmessage = true;
		bool ret = ParseJSon(data_, root);
		if ((!ret) || (!root.isObject()))
		{
			_log.Log(LOG_ERROR, "XiaomiGateway: invalid data received!");
			start_receive();
			return;
		}
		else {
			std::string cmd = root[xiaomi::key_cmd].asString();
			std::string model = root[xiaomi::key_model].asString();
			std::string sid = root[xiaomi::key_sid].asString();
			//std::string params = root[xiaomi::key_params].asString();
			Json::Value params = root[xiaomi::key_params];

			_log.Log(LOG_STATUS, "XiaomiGateway: cmd  %s received!", cmd.c_str());
			
			int unitcode = 1;
			if ((cmd == xiaomi::rsp_read) || (cmd ==xiaomi::rsp_write) || (cmd == xiaomi::rep_report) || (cmd == xiaomi::rep_hbeat))
			{
				Json::Value root2 = params[0];

				//if ((!root2.isObject()))
				if (1)
				{
					_eSwitchType type = STYPE_END;
					std::string name = "Xiaomi Switch";
					
					/* status */
					std::string status = "";

					bool commit = false;
					bool on = false;
					int level = -1;

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
							level = 0;
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
								commit = true;
							}
						}
						if (commit)
						{
							level = 0;
							TrueGateway->InsertUpdateSwitch(sid.c_str(), name, on, type, unitcode, level, cmd, "", "", battery);
						}
					}
					else if (model == "sensor_ht" || model == "weather.v1" || model == "weather")
					{
						if (model == "sensor_ht")
						{
							name = "Xiaomi Temperature/Humidity";
						}
						else if (model == "weather.v1" || model == "weather" )
						{
							name = "Xiaomi Aqara Weather";
						}
						
						std::string stemperature = "";
						std::string shumidity = "";
						std::string spressure = "";

						float ftemperature = 0.0;
						int   ihumidity = 0;
						float fpressure = 0.0;

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
					}
					/* 状态型 */
					else if (model == "plug" ||
							model == "86plug" || model == "ctrl_86plug.aq1" ||
							model == "ctrl_neutral1" || model == "ctrl_ln1" || model == "ctrl_ln1.aq1" ||
							model == "ctrl_neutral2" || model == "ctrl_ln2" || model == "ctrl_ln2.aq1")
					{
						commit = false;
						std::string load_power = "";
						std::string  consumed = "";
						
						type = STYPE_OnOff;
						if (model == "plug")
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
								commit = true;
								if (model == "ctrl_neutral1" || model == "ctrl_ln1" || model == "ctrl_ln1.aq1")
								{
									unitcode = 8;
									TrueGateway->InsertUpdateSwitch(sid.c_str(), name, on, type, unitcode, level, cmd, load_power, consumed, battery);
									commit = false;
								}
								else if (model == "ctrl_neutral2" || model == "ctrl_ln2" || model == "ctrl_ln2.aq1")
								{
									unitcode = 9;
									name = "Xiaomi Wired Dual Wall Switch Channel 0";
									TrueGateway->InsertUpdateSwitch(sid.c_str(), name, on, type, unitcode, level, cmd, load_power, consumed, battery);
									commit = false;
								}
							}
							else if(params[i].isMember("channel_1"))
							{
								status = params[i]["channel_1"].asString();
								on = (status ==  "on")?true : false;
								commit = true;
								if (model == "ctrl_neutral2" || model == "ctrl_ln2" || model == "ctrl_ln2.aq1")
								{
									unitcode =  10;
									name = "Xiaomi Wired Dual Wall Switch Channel 1";
									TrueGateway->InsertUpdateSwitch(sid.c_str(), name, on, type, unitcode, level, cmd, load_power, consumed, battery);
									commit = false;
								}
							}
							else if (params[i].isMember("load_power"))
							{
								load_power = params[i]["load_power"].asString();
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
							sleep_milliseconds(100);
							level = 0;
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
						int level = -1;
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
								status = params[i]["dual_channel"].asString();
							}
						} 

						if (status == "click")
						{
							level = (channel == 0)? 10:30;
							on = true;
						}
						else if (status == "double_click")
						{
							level = (channel == 0)? 20:40;
							on = true;
						}
						else if (status == "shake")
						{
							level = 30;
							on = true;
						}
						else if (status == "both_click")
						{
							level = 50;
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
						else if (status == "shake_air")
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
							status = status +"  "+ rdegree +"degree in "+ rtime +"ms";
							TrueGateway->InsertUpdateCubeText(sid.c_str(), name, status.c_str());
							TrueGateway->InsertUpdateSwitch(sid.c_str(), name, on, type, unitcode, level, cmd, "", "", battery);
						}
						
						
					}
					/* 动作型 */
					else if (model == "vibration" || model == "vibration.aq1")
					{
						name = "Aqara Vibration Sensor";
						type = STYPE_Selector;
						for (int i = 0; i < (int)params.size(); i++)
						{
							if(params[i].isMember("vibration_status"))
							{
								status = params[i]["vibration_status"].asString();
								commit = true;
							}
						}
						if (status == "tilt")
						{
							level = 10;
							on = true;
						}
						else if (status == "vibrate")
						{
							level = 20;
							on = true;
						}
						else if (status == "free_fall")
						{
							level = 30;
							on = true;
						}
						if (commit)
						{
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
							level = 0;
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
						//type = STYPE_Dimmer;

						on = false;
						std::string light_level = "";
						std::string light_rgb = "";
						for (int i = 0; i < (int)params.size(); i++)
						{
							if(params[i].isMember("power_status"))
							{
								status = params[i]["power_status"].asString();
								commit = true;
							}
							else if(params[i].isMember("light_rgb"))
							{
								light_rgb = params[i]["light_rgb"].asString();
								commit = true;
							}
							else if(params[i].isMember("light_level"))
							{
								light_level = params[i]["light_level"].asString();
								commit = true;
							}
						}
						if(status == "on" )
						{
							on = true;
						}

						if (commit)
						{
							std::stringstream  ss;
							std::string rgb_hex = "";
							std::string brightness_hex = "";
							if (light_rgb != "")
							{
								int irgb =  std::stoi(light_rgb);
								ss<<std::hex<<(irgb & 0xffffff);
								rgb_hex = ss.str();
								ss.str("");
								ss<<std::hex<<((irgb & 0xff000000) >>24);
								brightness_hex =  ss.str();
							}
							else if (light_level != "")
							{
								int tmp = std::stoi(light_level)*0x64 /100;
								ss<<std::hex<<tmp;
								brightness_hex = ss.str();
							}
							_log.Log(LOG_STATUS, "led: rgb_hex:%s   brightness_hex:%s", rgb_hex.c_str(), brightness_hex.c_str());
							TrueGateway->InsertUpdateRGBLight(sid, name, sTypeColor_RGB_CW_WW_Z, rgb_hex, brightness_hex, false, battery);
						}

					}
					else if (model == "gateway" || model == "gateway.v3" || model == "acpartner.v3")
					{
						name = "Xiaomi RGB Gateway";

						std::string rgb = root2["rgb"].asString();
						std::string illumination = root2["illumination"].asString();

						if (rgb != "") {
							// Only add in the gateway that matches the SID for this hardware.
							//if (m_XiaomiGateway->m_GatewaySID == sid) {
							if (TrueGateway->GetGatewaySid() == sid)
							{
								std::stringstream ss;
								ss << std::hex << atoi(rgb.c_str());
								std::string hexstring(ss.str());
								if (hexstring.length() == 7) {
									hexstring.insert(0, "0");
								}
								std::string bright_hex = hexstring.substr(0, 2);
								std::stringstream ss2;
								ss2 << std::hex << bright_hex.c_str();
								int brightness = strtoul(bright_hex.c_str(), NULL, 16);
								bool on = false;
								if (rgb != "0") {
									on = true;
								}
								TrueGateway->InsertUpdateRGBGateway(sid.c_str(), name + " (" + TrueGateway->GetGatewayIp() + ")", on, brightness, 0);
								TrueGateway->InsertUpdateLux(sid.c_str(), "Xiaomi Gateway Lux", atoi(illumination.c_str()), 255);
								TrueGateway->InsertUpdateSwitch(sid.c_str(), "Xiaomi Gateway Alarm Ringtone", false, STYPE_Selector, 3, 0, cmd, "", "", 255);
								TrueGateway->InsertUpdateSwitch(sid.c_str(), "Xiaomi Gateway Alarm Clock", false, STYPE_Selector, 4, 0, cmd, "", "", 255);
								TrueGateway->InsertUpdateSwitch(sid.c_str(), "Xiaomi Gateway Doorbell", false, STYPE_Selector, 5, 0, cmd, "", "", 255);
								TrueGateway->InsertUpdateSwitch(sid.c_str(), "Xiaomi Gateway MP3", false, STYPE_OnOff, 6, 0, cmd, "", "", 255);
								TrueGateway->InsertUpdateSwitch(sid.c_str(), "Xiaomi Gateway Volume", false, STYPE_Dimmer, 7, 0, cmd, "", "", 255);
								TrueGateway->InsertUpdateSwitch(sid.c_str(), "Xiaomi Gateway Join Button", false, STYPE_OnOff, 254, 0, cmd, "", "", 255);
								
							}
						}
						else
						{
							//check for token
							std::string token = root["token"].asString();
							std::string ip = root2["ip"].asString();

							if ((token != "") && (ip != "")) {
								XiaomiGatewayTokenManager::GetInstance().UpdateTokenSID(ip, token, sid);
								showmessage = false;
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
			}
			else if (cmd == xiaomi::rsp_discorey)
			{
				//Json::Value root2;
				//ret = ParseJSon(params.c_str(), root2);
				Json::Value root2 = root["dev_list"];
				std::string sid; 
				if ((ret) || (!root2.isObject()))
				{
					for (int i = 0; i < (int)root2.size(); i++) {
						sid = root2[i]["sid"].asString();
						std::string message = "{\"cmd\" : \"read\",\"sid\":\"";
						//message.append(root2[i].asString().c_str());
						message.append(sid.c_str());
						message.append("\"}");
						std::shared_ptr<std::string> message1(new std::string(message));
						boost::asio::ip::udp::endpoint remote_endpoint;
						remote_endpoint = boost::asio::ip::udp::endpoint(boost::asio::ip::address::from_string(TrueGateway->GetGatewayIp().c_str()), 9494);
						socket_.send_to(boost::asio::buffer(*message1), remote_endpoint);
					}
				}
				showmessage = false;
			}
			else if (cmd == xiaomi::rsp_whois)
			{
				if (model == "gateway" || model == "gateway.v3" || model == "acpartner.v3" || model == "gateway.aq1")
				{
					std::string ip = root["ip"].asString();
					// Only add in the gateway that matches the IP address for this hardware.
					if (ip == TrueGateway->GetGatewayIp())
					{
						_log.Log(LOG_STATUS, "XiaomiGateway: RGB Gateway Detected");
						TrueGateway->InsertUpdateRGBGateway(sid.c_str(), "Xiaomi RGB Gateway (" + ip + ")", false, 0, 100);
						//TrueGateway->InsertUpdateSwitch(sid.c_str(), "Xiaomi Gateway Alarm Ringtone", false, STYPE_Selector, 3, 0, cmd, "", "", 255);
						//TrueGateway->InsertUpdateSwitch(sid.c_str(), "Xiaomi Gateway Alarm Clock", false, STYPE_Selector, 4, 0, cmd, "", "", 255);
						//TrueGateway->InsertUpdateSwitch(sid.c_str(), "Xiaomi Gateway Doorbell", false, STYPE_Selector, 5, 0, cmd, "", "", 255);
						//TrueGateway->InsertUpdateSwitch(sid.c_str(), "Xiaomi Gateway MP3", false, STYPE_OnOff, 6, 0, cmd, "", "", 255);
						//TrueGateway->InsertUpdateSwitch(sid.c_str(), "Xiaomi Gateway Volume", false, STYPE_Dimmer, 7, 0, cmd, "", "", 255);
						TrueGateway->InsertUpdateSwitch(sid.c_str(), "Xiaomi Gateway Join Button", false, STYPE_OnOff, 254, 0, cmd, "", "", 255);

						//query for list of devices
						std::string message = "{\"cmd\" : \"discovery\"}";
						std::shared_ptr<std::string> message2(new std::string(message));
						boost::asio::ip::udp::endpoint remote_endpoint;
						remote_endpoint = boost::asio::ip::udp::endpoint(boost::asio::ip::address::from_string(TrueGateway->GetGatewayIp().c_str()), 9494);
						socket_.send_to(boost::asio::buffer(*message2), remote_endpoint);
					}
				}
				showmessage = false;
			}
			else
			{
				_log.Log(LOG_STATUS, "XiaomiGateway (ID=%d): unknown cmd received: %s, model: %s", TrueGateway->GetGatewayHardwareID(), cmd.c_str(), model.c_str());
			}
		}
		if (showmessage && m_OutputMessage) {
			_log.Log(LOG_STATUS, "%s", data_);
		}
		start_receive();
	}
	else {
		_log.Log(LOG_ERROR, "XiaomiGateway: error in handle_receive %s", error.message().c_str());
	}
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
