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
#include "../main/RFXtrx.h"
#include "XiaomiGateway.h"
#include <openssl/aes.h>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <inttypes.h>
#include <memory>
#include <exception>
#include <stdexcept>



#include <arpa/inet.h>
#include <netinet/in.h>


#ifndef WIN32
#include <ifaddrs.h>
#endif


#include "Xiaomi/Outlet.hpp"
#include "Xiaomi/DevAttr.hpp"
#include "Xiaomi/Device.hpp"
#include "Xiaomi/SupportDevice.hpp"


/*
Xiaomi (Aqara) makes a smart home gateway/hub that has support
for a variety of Xiaomi sensors.
They can be purchased on AliExpress or other stores at very
competitive prices.
Protocol is Zigbee and WiFi, and the gateway and
Domoticz need to be in the same network/subnet with multicast working
*/

#define round(a) ( int ) ( a + .5 )


namespace XiaoMi{

const char* keyCmd = "cmd";
const char* keyModel = "model";
const char* keySid = "sid";
const char* keyParams = "params";
const char* keyToken = "token";
const char* keyDevList ="dev_list";
const char* keyIp =	"ip";
const char* keyProtocol ="protocal";
const char* keyPort ="port";
const char* keyKey = "key";

const char* keyBattery = "battery_voltage";

const char* cmdWhoIs = "whois";
const char* rspWhoIs = "iam";

const char* cmdDiscorey = "discovery";
const char* rspDiscorey = "discovery_rsp";

const char* cmdRead = "read";
const char* rspRead = "read_rsp";

const char* cmdWrite = "write";
const char* rspWrite = "write_rsp";

const char* repReport = "report";
const char* repHBeat = "heartbeat";



}


using namespace XiaoMi;

std::list<XiaomiGateway*> XiaomiGateway::m_gwList;
std::mutex XiaomiGateway::m_gwListMutex;

AttrMap XiaomiGateway::m_attrMap;
DeviceMap XiaomiGateway::m_deviceMap;


bool XiaomiGateway::checkZigbeeMac(const std::string& mac)
{
	if (mac.size() != 16)
	{
		_log.Log(LOG_ERROR, "zigbee mac size not equal 16");
		return false;
	}

	int ii = 0;
	for (const auto& itt : mac)
	{
		if ((itt >= '0' &&  itt <= '9') ||
		(itt >= 'a' && itt <= 'f') ||
		(itt >= 'A' && itt <= 'F'))
		{
			ii++;
		}
	}
	return (ii == 16);
}


XiaomiGateway::XiaomiGateway(const int ID)
{
	m_HwdID = ID;
	m_bDoRestart = false;
	m_ListenPort9898 = false;
	m_bDevInfoInited = false;
	m_GatewayUPort = 0;
	if (m_attrMap.empty())
	{
		initDeviceAttrMap(devInfoTab, sizeof(devInfoTab)/sizeof(devInfoTab[0]));
	}
}

XiaomiGateway::~XiaomiGateway(void)
{
		std::cout<<"~XiaomiGateway"<<std::endl;
}



bool XiaomiGateway::StartHardware()
{
	RequestStart();

	m_bDoRestart = false;
	m_bDevInfoInited = false;

	//force connect the next first time
	m_bIsStarted = true;

	std::vector<std::vector<std::string> > result;
	result = m_sql.safe_query("SELECT Password, Address FROM Hardware WHERE Type=%d AND ID=%d AND Enabled=1", HTYPE_XiaomiGateway, m_HwdID);
	if (result.empty())
	{
		_log.Log(LOG_ERROR, "No find the hardware(type:%d, id=%d)", HTYPE_XiaomiGateway, m_HwdID);
		return false;
	}
	m_GatewayPassword = result[0][0].c_str();
	m_GatewayIp = result[0][1].c_str();


	m_OutputMessage = false;
	result = m_sql.safe_query("SELECT Value FROM UserVariables WHERE (Name == 'XiaomiMessage')");
	if (!result.empty())
	{
		m_OutputMessage = true;
	}

	_log.Log(LOG_STATUS, "XiaomiGateway (ID=%d): Delaying worker startup...", m_HwdID);
	sleep_seconds(5);

	XiaomiGatewayTokenManager::GetInstance();

	addGatewayToList();

	if (m_ListenPort9898)
	{
		_log.Log(LOG_STATUS, "XiaomiGateway (ID=%d): Selected as main Gateway", m_HwdID);
	}

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

// Use this function to get local ip addresses via getifaddrs when Boost.Asio approach fails
// Adds the addresses found to the supplied vector and returns the count
// Code from Stack Overflow - https://stackoverflow.com/questions/2146191
int XiaomiGateway::getLocalIpAddr(std::vector<std::string>& ip_addrs)
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
		if (ifa->ifa_addr == NULL  || !(ifa->ifa_flags & IFF_UP))
		{
			continue;
		}

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

void XiaomiGateway::Do_Work()
{
	_log.Log(LOG_STATUS, "XiaomiGateway (ID=%d): Worker started...", m_HwdID);
	boost::asio::io_service io_service;
	//find the local ip address that is similar to the xiaomi gateway
	try
	{
		boost::asio::ip::udp::resolver resolver(io_service);
		boost::asio::ip::udp::resolver::query query(boost::asio::ip::udp::v4(), m_GatewayIp, "");
		boost::asio::ip::udp::resolver::iterator endpoints = resolver.resolve(query);
		boost::asio::ip::udp::endpoint ep = *endpoints;
		boost::asio::ip::udp::socket socket(io_service);
		socket.connect(ep);
		boost::asio::ip::address addr = socket.local_endpoint().address();
		std::string compareIp = m_GatewayIp.substr(0, (m_GatewayIp.length() - 3));
		std::size_t found = addr.to_string().find(compareIp);
		if (found != std::string::npos)
		{
			m_LocalIp = addr.to_string();
			_log.Log(LOG_STATUS, "XiaomiGateway (ID=%d): Using %s for local IP address.", m_HwdID, m_LocalIp.c_str());
		}
	}
	catch (std::exception& e)
	{
		_log.Log(LOG_ERROR, "XiaomiGateway (ID=%d): Could not detect local IP address using Boost.Asio: %s", m_HwdID, e.what());
	}

	// try finding local ip using ifaddrs when Boost.Asio fails
	if (m_LocalIp == "")
	{
		try
		{
			// get first 2 octets of Xiaomi gateway ip to search for similar ip address
			std::string compareIp = m_GatewayIp.substr(0, (m_GatewayIp.length() - 3));
			_log.Log(LOG_STATUS, "XiaomiGateway (ID=%d): XiaomiGateway IP address starts with: %s", m_HwdID, compareIp.c_str());

			std::vector<std::string> ip_addrs;
			if (XiaomiGateway::getLocalIpAddr(ip_addrs) > 0)
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

		catch (std::exception& e)
		{
			_log.Log(LOG_STATUS, "XiaomiGateway (ID=%d): Could not find local IP address with ifaddrs: %s", m_HwdID, e.what());
		}
	}

	XiaomiGateway::xiaomi_udp_server udp_server(io_service, m_HwdID, m_GatewayIp, m_LocalIp, m_ListenPort9898, m_OutputMessage, false, this);
	boost::thread bt;
	boost::thread bt2;
	if (m_ListenPort9898)
	{
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
	rmFromGatewayList();
	_log.Log(LOG_STATUS, "XiaomiGateway (ID=%d): stopped", m_HwdID);
}


XiaomiGateway * XiaomiGateway::getGatewayByIp(std::string ip)
{
	XiaomiGateway * ret = NULL;
	{
		std::unique_lock<std::mutex> lock(m_gwListMutex);
		for (const auto& itt : m_gwList)
		{
			if (itt->getGatewayIp() == ip)
			{
				_log.Debug(DEBUG_HARDWARE, "XiaomiGateway: find a gateway ip:%s at list", ip.c_str());
				ret = itt;
				break;
			}
		}
	}
	return ret;
}

void XiaomiGateway::addGatewayToList()
{
	XiaomiGateway * maingw = NULL;
	{
		std::unique_lock<std::mutex> lock(m_gwListMutex);
		for (const auto& itt : m_gwList)
		{
			if (itt->IsMainGateway())
			{
				maingw = (itt);
				break;
			}
		}

		if (!maingw)
		{
			SetAsMainGateway();
		}
		else
		{
			maingw->UnSetMainGateway();
		}
		m_gwList.push_back(this);
	}
	if (maingw)
	{
		maingw->Restart();
	}
}

void XiaomiGateway::rmFromGatewayList()
{
	XiaomiGateway * maingw = NULL;
	{
		std::unique_lock<std::mutex> lock(m_gwListMutex);
		m_gwList.remove(this);
		if (IsMainGateway())
		{
			UnSetMainGateway();
			if (!m_gwList.empty())
			{
				auto  it = m_gwList.begin();
				maingw = (*it);
			}
		}
	}

	if (maingw)
	{
		maingw->Restart();
	}
}

void XiaomiGateway::initDeviceAttrMap(const DevInfo devInfo[], int size)
{
	int ii;
	for (ii = 0; ii < size; ii++)
	{
		m_attrMap.emplace(devInfo[ii].zigbeeModel, new DevAttr(devInfo[ii]));
	}
	std::cout<<"support device number:"<<m_attrMap.size()<<std::endl;
	for (const auto & itt : m_attrMap)
	{
		std::cout<<"zigbee model:"<<itt.first<<std::endl;
	}
	return;
}

const DevAttr* XiaomiGateway::findDevAttr(std::string& model)
{
	auto it = m_attrMap.find(model);
	if (it == m_attrMap.end())
	{
		std::cout<<"error: not support zigbee model:"<<model<<std::endl;
		return nullptr;
	}
	return it->second;
}

void XiaomiGateway::addDeviceToMap(std::string& mac, std::shared_ptr<Device> ptr)
{
	if (!ptr)
	{
		_log.Log(LOG_ERROR, "add device to map, device null");
		return;
	}
	m_deviceMap.emplace(mac, ptr);
	std::cout<<"add device to map mac:"<<mac<<std::endl;
}

void XiaomiGateway::delDeviceFromMap(std::string& mac)
{
	m_deviceMap.erase(mac);
	std::cout<<"delDeviceFromMap mac:"<<mac<<std::endl;
}

std::shared_ptr<Device> XiaomiGateway::getDevice(std::string& mac)
{
	std::shared_ptr<Device> ptr;
	auto it = m_deviceMap.find(mac);
	if (it == m_deviceMap.end())
	{
		std::cout<<"error: getDevice failed mac:"<<mac<<std::endl;
		return nullptr;
	}
	return it->second;
}

std::shared_ptr<Device> XiaomiGateway::getDevice(unsigned int ssid, int type, int subType, int unit)
{
	bool res;
	std::shared_ptr<Device> ptr;
	for (const auto itt : m_deviceMap)
	{
		res = itt.second->match(ssid, type, subType, unit);
		if (true == res)
		{
			ptr = itt.second;
			break;
		}
	}
	if (ptr.get() == nullptr)
	{
		_log.Log(LOG_ERROR, "getDevice failed ssid:%x  type:%x  subType:%x  unit:%d", ssid, type, subType, unit);
		return nullptr;
	}
	return ptr;
}





bool XiaomiGateway::createWriteParam(const char * pdata, int length, WriteParam& param, std::shared_ptr <Device>& dev)
{
	if (pdata == nullptr)
	{
		_log.Log(LOG_ERROR, "message ptr null");
		return false;
	}
	const tRBUF *pCmd = reinterpret_cast<const tRBUF *>(pdata);
	unsigned char packetType = pCmd->ICMND.packettype;
	unsigned char subType = pCmd->ICMND.subtype;
	unsigned int ssid = 0;
	unsigned char unit = 0;
	switch(packetType)
	{
		case pTypeGeneralSwitch:
		{
			const _tGeneralSwitch *xcmd = reinterpret_cast<const _tGeneralSwitch*>(pCmd);
			ssid = xcmd->id;
			unit = xcmd->unitcode;
		}
		break;
		case pTypeColorSwitch:
		{
			const _tColorSwitch *xcmd = reinterpret_cast<const _tColorSwitch*>(pCmd);
			ssid = xcmd->id;
			unit = xcmd->dunit;
		}
		break;
		case pTypeMannageDevice:
		{
			ssid = TbGateway::idConvert(m_GatewaySID).first;
			unit = 1;
		}
		break;
		default:
			_log.Log(LOG_ERROR, "no support type on write");
			return false;
		break;
	}

	param.packet = reinterpret_cast<const unsigned char*>(pCmd);
	param.len = length;
	param.type = packetType;
	param.subType = subType;
	param.unit =unit;
	param.gwMac = m_GatewaySID;
	param.miGateway = static_cast<void*>(this);

	dev.reset();
	dev =	XiaomiGateway::getDevice(ssid,
							static_cast<int>(packetType),
							static_cast<int>(subType),
							static_cast<int>(unit));
	if (!dev)
	{
		_log.Log(LOG_ERROR, "no find want to write device");
		return false;
	}

	if (packetType == pTypeMannageDevice)
	{
		const tRBUF *xcmd = reinterpret_cast<const tRBUF *>(pdata);
		if (xcmd->MANNAGE.cmnd != cmdRmDevice)
		{
			return true;
		}
		int rmSsid = xcmd->MANNAGE.id1 << 24 | xcmd->MANNAGE.id2 << 16 |
					xcmd->MANNAGE.id3 << 8 | xcmd->MANNAGE.id4;
		int rmType = xcmd->MANNAGE.value1;
		int rmSubType = xcmd->MANNAGE.value2;
		int rmUnit = xcmd->MANNAGE.value3;

		std::shared_ptr <Device> rmDev =
			XiaomiGateway::getDevice(rmSsid, rmType, rmSubType, rmUnit);

		if (!rmDev)
		{
			_log.Log(LOG_ERROR, "no find want to delete device");
			return false;
		}
		param.rmMac = rmDev->getMac();
	}
	return true;
}

bool XiaomiGateway::WriteToHardware(const char * pdata, const unsigned char length)
{
	bool result = true;
	WriteParam param;
	std::shared_ptr<Device> dev;
	result = createWriteParam(pdata, length, param, dev);
	if(false == result)
	{
		_log.Log(LOG_ERROR, "pdate param check error");
		return false;
	}

	const tRBUF *pCmd = reinterpret_cast<const tRBUF *>(pdata);
	unsigned char packettype = pCmd->ICMND.packettype;

	if (m_GatewaySID == "")
	{
		m_GatewaySID = XiaomiGatewayTokenManager::GetInstance().GetSID(m_GatewayIp);
	}

	result = dev->writeTo(param);
	if (false == result)
	{
		sleep_milliseconds(200);
		result = dev->writeTo(param);
	}

	if (result && packettype == pTypeMannageDevice )
	{
		const tRBUF *xcmd = reinterpret_cast<const tRBUF *>(pdata);
		if (xcmd->MANNAGE.cmnd == cmdRmDevice)
		{
			delDeviceFromMap(param.rmMac);
		}
	}
	return result;
}

bool XiaomiGateway:: sendMessageToGateway(const std::string &controlmessage) {
	if (m_GatewayUPort == 0)
	{
		_log.Log(LOG_ERROR, "Please shakehand to get nicast port first");
		return false;
	}
	std::string message = controlmessage;
	bool result = true;
	boost::asio::io_service io_service;
	boost::asio::ip::udp::socket socket_(io_service, boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), 0));
	stdreplace(message, "@gatewaykey", GetGatewayKey());
	std::shared_ptr<std::string> message1(new std::string(message));
	boost::asio::ip::udp::endpoint remote_endpoint_;
	remote_endpoint_ = boost::asio::ip::udp::endpoint(boost::asio::ip::address::from_string(m_GatewayIp), m_GatewayUPort);
	socket_.send_to(boost::asio::buffer(*message1), remote_endpoint_);
	sleep_milliseconds(50);

	_log.Log(LOG_STATUS,  "send to GW :%s", message.c_str());
#if 0
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
#endif
	socket_.close();
	return result;
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
#ifdef _DEBUG
	_log.Log(LOG_STATUS, "XiaomiGateway: GetGatewayKey Password - %s, token=%s", m_GatewayPassword.c_str(), token.c_str());
	_log.Log(LOG_STATUS, "XiaomiGateway: GetGatewayKey key - %s", gatewaykey);
#endif
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

void XiaomiGateway::deviceListHandler(Json::Value& root)
{
	std::string gwSid;
	std::string token;
	std::string sid;
	std::string model;
	std::string message;

	gwSid = root[keySid].asString();
	token = root[keyToken].asString();
	if (!XiaomiGateway::getDevice(gwSid))
	{
		const DevAttr* attr = XiaomiGateway::findDevAttr(m_gwModel);
		if (attr == nullptr)
		{
			_log.Log(LOG_ERROR, "XiaomiGateway: No support the gateway(%s, %s)", m_gwModel.c_str(), gwSid.c_str());
			return;
		}

		std::shared_ptr<Device> dev(new Device (gwSid, attr));
		XiaomiGateway::addDeviceToMap(gwSid, dev);
		m_GatewaySID = gwSid;
	}

	XiaomiGatewayTokenManager::GetInstance().UpdateTokenSID(m_GatewayIp, token, gwSid);
	SetDevInfoInited(true);

	Json::Value list = root[keyDevList];
	for (int ii = 0; ii < (int)list.size(); ii++)
	{

		if (false == list[ii].isMember(keySid) ||
			false == list[ii].isMember(keyModel) ||
			false == list[ii][keySid].isString() ||
			false == list[ii][keyModel].isString())
		{
			_log.Log(LOG_ERROR, "XiaomiGateway: dev_list sid or model not in json");
			continue;
		}

		sid = list[ii][keySid].asString();
		model = list[ii][keyModel].asString();

		if (sid.empty() || model.empty() || !checkZigbeeMac(sid))
		{
			_log.Log(LOG_ERROR, "XiaomiGateway: dev_list sid or model empty or mac invalid");
			continue;
		}
		/* if device support */
		const DevAttr* attr = XiaomiGateway::findDevAttr(model);
		if(nullptr == attr)
		{
			Json::Value del_cmd;
			del_cmd[keyCmd] = "write";
			del_cmd[keyKey] = "@gatewaykey";
			del_cmd[keySid] = gwSid;
			del_cmd[keyParams][0]["remove_device"] = sid;
			message = JSonToRawString (del_cmd);
			std::shared_ptr<std::string> send_buff(new std::string(message));
			sendMessageToGateway(message);
			_log.Log(LOG_ERROR, "XiaomiGateway: not support the device model:%s  mac:%s", sid.c_str(), model.c_str());
			continue;
		}
		/* if device not exist */
		if (!XiaomiGateway::getDevice(sid))
		{
			std::shared_ptr<Device> dev(new Device (sid, attr));
			XiaomiGateway::addDeviceToMap(sid, dev);
			createDtDevice(dev);
		}

		Json::Value read_cmd;
		read_cmd[keyCmd] = "read";
		read_cmd[keySid] = sid;
		message = JSonToRawString (read_cmd);
		sendMessageToGateway(message);
	}

}

void XiaomiGateway::joinGatewayHandler(Json::Value& root)
{
	std::string model = root["model"].asString();
	std::string ip = root["ip"].asString();
	std::string port =	root["port"].asString();

	if (ip != getGatewayIp())
	{
		_log.Log(LOG_ERROR, "a new gateway incoming, but not in domoticz hardware list, ip:%s", ip.c_str());
		return;
	}

	m_GatewayUPort = std::stoi(port);
	m_gwModel = model;

	const DevAttr* attr = XiaomiGateway::findDevAttr(m_gwModel);
	if(attr == nullptr)
	{
		_log.Log(LOG_ERROR, "No support Gateway model:%s", model.c_str());
		return;
	}

	std::string message = "{\"cmd\" : \"discovery\"}";
	sendMessageToGateway(message);
	_log.Log(LOG_STATUS, "Tenbat Gateway(%s) Detected", model.c_str());
}

static inline int customImage(int switchtype)
{
	int customimage = 0;
	if (customimage == 0)
	{
		if (switchtype == static_cast<int>(STYPE_OnOff))
		{
			customimage = 1; //wall socket
		}
		else if (switchtype == static_cast<int>(STYPE_Selector))
		{
			customimage = 9;
		}
	}
	return customimage;
}

bool XiaomiGateway::createDtDevice(std::shared_ptr<Device> dev)
{
	if(!dev)
	{
		_log.Log(LOG_ERROR, "create Dt Device param dev null");
		return false;
	}

	int ii;
	int type, subType, switchType, uint, outlet;
	unsigned int ssid;
	uint64_t DeviceRowIdx = (uint64_t)-1;

	std::string name = dev->getName();
	std::string model = dev->getZigbeeModel();
	std::string mac = dev->getMac();
	auto outletAttr = dev->getOutlet();
	auto ssidPair =  dev->getSsid();

	std::string devID = "";
	std::string devOpt = "";
	std::string opts = "";

	if (ssidPair.size() != outletAttr.size())
	{
		_log.Log(LOG_ERROR,  "outlet number(%lu) not equal ssid number(%lu)", ssidPair.size(), outletAttr.size());
		return false;
	}

	std::cout<<name<<std::endl;
	std::cout<<model<<std::endl;
	std::cout<<mac<<std::endl;

	ii = 0;
	for (const auto itt : outletAttr)
	{
		type = itt->getType();
		subType = itt->getSubType();
		switchType = itt->getSWType();
		uint = itt->getUnit();
		outlet = ii;
		devOpt = itt->getOpts();
		ssid = ssidPair[ii].first;
		devID = ssidPair[ii].second;
		ii++;
		if(m_sql.DoesDeviceExist(m_HwdID, devID.c_str(), uint, type, subType) == true)
		{
			continue;
		}
		bool bPrevAcceptNewHardware = m_sql.m_bAcceptNewHardware;
		m_sql.m_bAcceptNewHardware = true;
		DeviceRowIdx = m_sql.CreateDevice(m_HwdID, type, subType, name, ssid, opts, uint);
		m_sql.m_bAcceptNewHardware = bPrevAcceptNewHardware;

		if (DeviceRowIdx == (uint64_t)-1)
		{
			_log.Log(LOG_ERROR,  "add new device failed model:%s, ssid:%08X", model.c_str(), ssid);
			continue;
		}
		int cImage = customImage(switchType);
		m_sql.safe_query("UPDATE DeviceStatus SET Outlet=%d, SwitchType=%d, CustomImage=%i, Model='%q', Mac='%q' WHERE (HardwareID == %d) AND (ID == %" PRIu64 ")", outlet, switchType, cImage,  model.c_str(), mac.c_str(), m_HwdID, DeviceRowIdx);
		if (devOpt != "")
		{
			m_sql.SetDeviceOptions(DeviceRowIdx, m_sql.BuildDeviceOptions(devOpt, false));
		}
		_log.Log(LOG_STATUS,  "add new device successful model:%s, ssid:%08X", model.c_str(), ssid);
	}

	return true;
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
	float lasttmep= 0.0;
	int   lasthum = 0;
	int   lasthumstatus = 0;
	int   lastpre = 0;
	int   lastorcast = 0;
	float temp = 0.0;
	int   hum  = 0;
	float pre  = 0.0;

	temp = (float)atof(Temperature.c_str()) / 100.0f;
	hum  =  atoi(Humidity.c_str()) / 100;
	pre  = static_cast<float>(atof(Pressure.c_str())) / 100.0f;

	if (Temperature == "" || Humidity == "" || Pressure == "")
	{
		auto ssid = WeatherOutlet::idConvert(nodeid);
		std::string svalue = "";
		int type    = pTypeTEMP_HUM_BARO;
		int subtype = sTypeTHB1;
		int nvalue  = 0;
		struct tm updatetime;

		std::cout<<"ssid.first"<<ssid.first<<"ssid.second"<<ssid.second<<std::endl;

		res = m_sql.GetLastValue(m_HwdID, ssid.second.c_str(), 1, type, subtype, nvalue, svalue, updatetime);
		if(false == res)
		{
			_log.Log(LOG_ERROR, "get TempHumPressure svalue failed");
			return;
		}

		int n = sscanf(svalue.c_str(), "%f;%d;%d;%d;%d",  &lasttmep, &lasthum, &lasthumstatus, &lastpre, &lastorcast);
		if (n == 5)
		{
			if (Temperature == "")
			{
				temp = lasttmep;
			}
			if (Humidity == "")
			{
				hum = lasthum;
			}
			if (Pressure == "")
			{
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

	unsigned int sID = GetShortID(nodeid);
	if (sID > 0)
	{
		SendTempHumBaroSensor(sID, battery, temp, hum, pre, barometric_forcast, Name);
	}
}

void XiaomiGateway::InsertUpdateTempHum(const std::string &nodeid, const std::string &Name, const std::string& Temperature, const std::string&  Humidity, const int battery)
{
	float lasttmep= 0.0;
	int   lasthum = 0;
	int   lasthumstatus = 0;
	float temp = 0.0;
	int   hum  = 0;
	bool  res = true;

	temp = (float)atof(Temperature.c_str()) / 100.0f;
	hum =  atoi(Humidity.c_str()) / 100;

	if (Temperature == "" || Humidity == "")
	{
		auto ssid = WeatherOutlet::idConvert(nodeid);
		std::string svalue = "";
		int type    = pTypeTEMP_HUM;
		int subtype = sTypeTH5;
		int nvalue  = 0;
		struct tm updatetime;

		std::cout<<"ssid.first"<<ssid.first<<"ssid.second"<<ssid.second<<std::endl;

		res = m_sql.GetLastValue(m_HwdID, ssid.second.c_str(), 1, type, subtype, nvalue, svalue, updatetime);
		if(false == res)
		{
			_log.Log(LOG_ERROR, "get TempHumPressure svalue failed");
			return;
		}

		int n = sscanf(svalue.c_str(), "%f;%d;%d",  &lasttmep, &lasthum, &lasthumstatus);
		if (n == 3)
		{
			if (Temperature == "")
			{
				temp = lasttmep;
			}
			if (Humidity == "")
			{
				hum = lasthum;
			}
		}
	}

	unsigned int sID = GetShortID(nodeid);
	if (sID > 0)
	{
		SendTempHumSensor(sID, battery, temp, hum, Name);
	}
}


void XiaomiGateway::InsertUpdateRGBLight(const std::string & NodeID,const unsigned char Unit, const unsigned char SubType,const int OnOff, const std::string& Brightness, const _tColor&  Color, const int battery)
{
	unsigned int sID = GetShortID(NodeID);
	if (sID > 0)
	{
		int lastLevel = 0;
		int nvalue = 0;
		int bright = 0;

		bright = atoi(Brightness.c_str());

		std::cout<<"InsertUpdateRGBLight bright string:"<<Brightness<<"  bright:"<<bright<<std::endl;
		std::cout<<"color json:"<<Color.toJSONString()<<std::endl;

		std::vector<std::vector<std::string> > result;

		auto ssid = LedOutlet::idConvert(NodeID);
		result = m_sql.safe_query("SELECT nValue, LastLevel FROM DeviceStatus WHERE (HardwareID==%d) AND (DeviceID=='%q') AND (Type==%d)", m_HwdID, ssid.second.c_str(), pTypeColorSwitch);
		if (result.empty())
		{
			return;
		}

		nvalue = atoi(result[0][0].c_str());
		lastLevel = atoi(result[0][1].c_str());
		bright = (Brightness == "")? lastLevel : bright;

		if (nvalue == gswitch_sOff)
		{
			_log.Log(LOG_ERROR, "led off , do not update all value");
			return;
		}
		SendRGBWSwitch(sID, Unit, SubType, OnOff, bright, Color, battery);

		std::cout<<"nvalue:"<<nvalue<<"	 lastlevel:"<<lastLevel<<"  bright:"<<bright<<std::endl;;
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
		m_sql.safe_query("UPDATE DeviceStatus SET Name='%q', SwitchType=%d, LastLevel=%d WHERE(HardwareID == %d) AND (DeviceID == '%s') AND (Type == %d)", Name.c_str(), static_cast<int>(STYPE_Dimmer), brightness, m_HwdID, szDeviceID, pTypeColorSwitch);

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
					m_sql.SetDeviceOptions(atoi(Idx.c_str()), m_sql.BuildDeviceOptions("SelectorStyle:0;LevelNames:Off|Click|Double Click|Long Click", false));
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
					m_sql.SetDeviceOptions(atoi(Idx.c_str()), m_sql.BuildDeviceOptions("SelectorStyle:0;LevelNames:Off|vibration|tilt|drop", false));
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

void XiaomiGateway::InsertUpdateKwh(const std::string & nodeid, const std::string & Name, const std::string& LoadPower, const std::string& Consumed, const int battery)
{
	unsigned int sID = GetShortID(nodeid);
	if (sID > 0) {
		if (LoadPower != "" || Consumed != "")
		{
			double power = atof(LoadPower.c_str());
			double consumed = atof(Consumed.c_str()) / 1000;
			SendKwhMeter(sID>>8 , sID & 0xff, 255, power, consumed, Name);
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

	boost::asio::io_service unicast_sender;
	boost::asio::ip::udp::endpoint local_addr(boost::asio::ip::address_v4::from_string("0.0.0.0"), 0);
	boost::asio::ip::udp::endpoint remote_addr(boost::asio::ip::address::from_string("224.0.0.50"), 4321);
	boost::asio::ip::udp::socket socket_wh(unicast_sender, local_addr);
	_log.Log(LOG_STATUS, "whois thread start");

	std::shared_ptr<std::string> message(new std::string("{\"cmd\":\"whois\"}"));
	while (1)
	{
		boost::this_thread::sleep(boost::posix_time::seconds(5));

		sec_counter++;
		if (sec_counter % 3 == 0)
		{
			XiaomiGateway * TrueGateway = m_XiaomiGateway->getGatewayByIp(m_gatewayip);

			if (!TrueGateway)
			{
				continue;
			}

			if (false == TrueGateway->IsDevInfoInited())
			{
				_log.Log(LOG_STATUS, "send whois at thread");
				socket_wh.send_to(boost::asio::buffer(*message), remote_addr);
			}
		}
	}
}





bool XiaomiGateway::xiaomi_udp_server::recvParamCheck(Json::Value& root)
{
	if (false == root.isMember(keyCmd) ||
		false == root.isMember(keyModel)||
		false == root[keyCmd].isString() ||
		false == root[keyModel].isString())
	{
		_log.Log(LOG_ERROR, "ParamCheck: Receive the message cmd or model not exist or not string");
		return false;
	}

	std::string cmd = root[keyCmd].asString();
	std::string model = root[keyModel].asString();
	if (cmd.empty() || model.empty())
	{
		_log.Log(LOG_ERROR, "ParamCheck: Receive the message cmd or model empty");
		return false;
	}

	/* cmd model */
	if ((cmd == rspRead) ||
		(cmd == rspWrite) ||
		(cmd == repReport) ||
		(cmd == repHBeat))
	{
		if (false == root.isMember(keySid) ||
			false == root.isMember(keyParams)||
			false == root[keySid].isString() ||
			false == root[keyParams].isArray())
		{
			_log.Log(LOG_ERROR, "ParamCheck: Receive the message cmd or model error");
			return false;
		}

		std::string sid = root[keySid].asString();
		if (sid.empty())
		{
			_log.Log(LOG_ERROR, "ParamCheck: Receive the message sid empty");
			return false;
		}

		if (!checkZigbeeMac(sid))
		{
			_log.Log(LOG_ERROR, "ParamCheck: Device sid(mac) is invalid");
			return false;
		}

		Json::Value params =  root[keyParams];
		if (params.size() <= 0)
		{
			_log.Log(LOG_ERROR, "ParamCheck: Params is null at json");
			return false;
		}
		return true;
	}


	if (cmd == rspDiscorey)
	{
		if (false == root.isMember(keySid) ||
			false == root.isMember(keyToken)||
			false == root.isMember(keyDevList)||
			false == root[keySid].isString() ||
			false == root[keyToken].isString() ||
			false == root[keyDevList].isArray())
		{
			_log.Log(LOG_ERROR, "Receive the message miss sid token dev_list error");
			return false;
		}

		std::string sid = root[keySid].asString();
		std::string token = root[keyToken].asString();
		if (sid.empty() || token.empty())
		{
			_log.Log(LOG_ERROR, "ParamCheck: sid or token is empty at json");
			return false;
		}

		if (!checkZigbeeMac(sid))
		{
			_log.Log(LOG_ERROR, "ParamCheck: Device sid(mac) is invalid");
			return false;
		}

		return true;
	}


	if (cmd == rspWhoIs)
	{
		if (false == root.isMember(keyIp) ||
			false == root.isMember(keyProtocol) ||
			false == root.isMember(keyPort) ||
			false == root[keyIp].isString() ||
			false == root[keyProtocol].isString() ||
			false == root[keyPort].isString())
		{
			_log.Log(LOG_ERROR, "ParamCheck: Receive the message miss ip protocal port");
			return false;
		}

		std::string ip = root[keyIp].asString();
		std::string protocol = root[keyProtocol].asString();
		std::string port = root[keyPort].asString();
		if (ip.empty() || protocol.empty() || port.empty())
		{
			_log.Log(LOG_ERROR, "ParamCheck: Receive the message ip protocal port value null");
			return false;
		}

		struct sockaddr_in addr;
		int ret = ::inet_pton(AF_INET, ip.c_str(), (void*)&addr.sin_addr.s_addr);
		if (ret != 1)
		{
			_log.Log(LOG_ERROR, "ParamCheck: Receive the message ip convert error");
			return false;
		}

		try
		{
			ret = std::stoi(port);
			if (ret < 1024 || ret > 65535)
			{
				throw std::range_error("port outof range");
			}
		}

		catch(std::exception & e)
		{
			_log.Log(LOG_ERROR, "ParamCheck: Receive the message port(%s) error: %s", port.c_str(),  e.what());
			return false;
		}

		if (protocol != "UDP")
		{
			_log.Log(LOG_ERROR, "ParamCheck: Receive the message protocol value error, expect UDP");
			return false;
		}
		return true;
	}

	_log.Log(LOG_ERROR, "ParamCheck: Cmd not support, cmd:%s", cmd.c_str());
	return false;
}



void XiaomiGateway::xiaomi_udp_server::handle_receive(const boost::system::error_code & error, std::size_t bytes_recvd)
{

	if (error && error != boost::asio::error::message_size)
	{
		_log.Log(LOG_ERROR, "XiaomiGateway: error in handle_receive %s", error.message().c_str());
		return;
	}

	std::string remoteAddr = remote_endpoint_.address().to_v4().to_string();
	XiaomiGateway * TrueGateway = m_XiaomiGateway->getGatewayByIp(remoteAddr);
	if (!TrueGateway)
	{
		_log.Log(LOG_ERROR, "XiaomiGateway: received data from  unregisted gateway ip:%s!", remoteAddr.c_str());
		start_receive();
		return;
	}

#define _DEBUG
#ifdef _DEBUG
	_log.Log(LOG_STATUS, "%s", data_);
#endif

	Json::Value root;
	bool showmessage = true;
	std::string message(data_);
	bool ret;

	ret = ParseJSon(message, root);
	if (false == ret || false == root.isObject())
	{
		_log.Log(LOG_ERROR, "XiaomiGateway: the received data json format error!");
		start_receive();
		return;
	}

	if (!recvParamCheck(root))
	{
		_log.Log(LOG_ERROR, "XiaomiGateway: param check error!");
		start_receive();
		return;
	}

	Json::Value params;
	std::string cmd;
	std::string model;
	std::string sid;

	cmd = root[keyCmd].asString();
	model = root[keyModel].asString();

	if ((cmd == rspRead) ||
		(cmd == rspWrite) ||
		(cmd == repReport) ||
		(cmd == repHBeat))
	{
		sid = root[keySid].asString();
		std::shared_ptr <Device> dev = XiaomiGateway::getDevice(sid);
		if (!dev)
		{
			_log.Log(LOG_ERROR, "XiaomiGateway: not find the device at list! sid:%s", sid.c_str());
			start_receive();
			return;
		}
		if(dev->getZigbeeModel() != model)
		{
			_log.Log(LOG_ERROR, "XiaomiGateway: model not match, expect:%s, but:%s sid:%s",
					dev->getZigbeeModel().c_str(), model.c_str(), sid.c_str());
			start_receive();
			return;
		}

		ReadParam param;
		param.message  = message;
		param.miGateway = static_cast<void*>(TrueGateway);
		dev->recvFrom(param);
		start_receive();
		return;
	}


	if (cmd == rspDiscorey)
	{
		TrueGateway->deviceListHandler(root);
		start_receive();
		return;
	}


	if (cmd == rspWhoIs)
	{
		TrueGateway->joinGatewayHandler(root);
		start_receive();
		return;
	}

	_log.Log(LOG_STATUS, " recv Param Check have some problem, call soft developer cmd:%s", cmd.c_str());
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
			std::string cmd = request::findValue(&req, "command");
			std::string model = request::findValue(&req, "model");

			if (hwid == "" || (cmd != "On" && cmd != "Off"))
			{
				return;
			}

			int iHardwareID = atoi(hwid.c_str());
			CDomoticzHardwareBase *pHardware = m_mainworker.GetHardware(iHardwareID);
			if (pHardware == NULL)
			{
				return;
			}

			if (pHardware->HwdType != HTYPE_XiaomiGateway)
			{
				return;
			}

			root["status"] = "OK";
			root["title"] = "AddZigbeeDevice";
			m_mainworker.AddZigbeeDevice(hwid, cmd, model);
		}

		void CWebServer::Cmd_GetNewDevicesList(WebEmSession & session, const request& req, Json::Value &root)
		{
			static int sHwId = 0;
			static int sRandom = 0;
			static std::map< std::string, std::vector<std::string> > sOldList;

			if (session.rights != 2)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}

			std::string hwid = request::findValue(&req, "idx");
			std::string random = request::findValue(&req, "random");
			if (hwid == "" || random == "")
			{
				return;
			}

			int tmp;
			int iHardwareID;

			try{
				tmp = std::stoi(random);
				iHardwareID = std::stoi(hwid);
			}

			catch(std::exception& e)
			{
				_log.Log(LOG_ERROR, "get random(%s) or hardware(%s) id failed:%s", random.c_str(), hwid.c_str(), e.what());
			}

			std::cout<<"business random:"<<sRandom<< "  now:"<<tmp<<std::endl;
			root["status"] = "OK";
			root["title"] = "GetNewDevicesList";

			std::vector<std::vector<std::string> > result;
			result = m_sql.safe_query("SELECT Mac, Name, Model FROM DeviceStatus WHERE (HardwareID=%d)", iHardwareID);

			/* start of get new list */
			if(tmp != sRandom)
			{
				sRandom	= tmp;
				sOldList.clear();
				for (const auto &itt : result)
				{
					sOldList.insert(std::make_pair(itt[0], itt));
				}
				/* if dev_list null, add null at [0] */
				root["dev_list"][0];
				return;
			}

			int ii = 0;

			for (const auto &itt : result)
			{
				auto iter = sOldList.find(itt[0]);
				/* no find at old list */
				if (iter == sOldList.end())
				{
					root["dev_list"][ii]["Name"] = itt[1];
					root["dev_list"][ii]["Model"] = itt[2];
					root["dev_list"][ii]["Mac"] = itt[0];
					ii++;
				}
			}
			/* if dev_list null, add null at [0] */
			if (0 == ii)
			{
				root["dev_list"][0];
			}

		}

		}
}
