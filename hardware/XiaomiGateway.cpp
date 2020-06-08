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
#include <boost/system/system_error.hpp>
#include <inttypes.h>
#include <memory>
#include <exception>
#include <stdexcept>
#include <arpa/inet.h>
#include <netinet/in.h>

#include "Xiaomi/Outlet.hpp"
#include "Xiaomi/DevAttr.hpp"
#include "Xiaomi/Device.hpp"
#include "Xiaomi/SupportDevice.hpp"

#ifndef WIN32
#include <ifaddrs.h>
#endif




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
const char* repNewDevList = "new_dev_list";

}


using namespace XiaoMi;

/* Gateway online list and mutex */
std::list<XiaomiGateway*> XiaomiGateway::m_gwList;
std::mutex XiaomiGateway::m_gwListMutex;


/* support device list and online device list */
AttrMap XiaomiGateway::m_attrMap;
std::shared_ptr<boost::thread> XiaomiGateway::m_whoIsThread;

UdpServer* UdpServer::m_updServer = nullptr;
std::mutex UdpServer::m_mutex;
volatile bool UdpServer::m_isRun = false;


bool XiaomiGateway::checkZigbeeMac(const std::string& mac)
{
	if (mac.size() != 16  && mac.size() != 8){
		_log.Log(LOG_ERROR, "zigbee mac size not equal 16");
		return false;
	}

	int ii = 0;
	for (const auto& itt : mac){

		if ((itt >= '0' &&  itt <= '9') ||
		(itt >= 'a' && itt <= 'f') ||
		(itt >= 'A' && itt <= 'F')){
			ii++;
		}
	}
	return (ii == mac.size());
}


XiaomiGateway::XiaomiGateway(const int ID)
{
	_log.Log(LOG_STATUS, "new XiaomiGateway");
	m_HwdID = ID;
	m_outputMessage = false;
	m_devInfoInited = false;
	m_gwUnicastPort = 0;
	if (m_attrMap.empty()){
		initDeviceAttrMap(devInfoTab, sizeof(devInfoTab)/sizeof(devInfoTab[0]));
	}
}

XiaomiGateway::~XiaomiGateway(void)
{
	_log.Log(LOG_STATUS, "~XiaomiGateway");

	int gwNum = gatewayListSize();

	std::unique_lock<std::mutex> lock(UdpServer::m_mutex);
	auto server = UdpServer::getUdpServer();

	if (gwNum == 0 && server != nullptr){
		UdpServer::resetUdpServer();
		server->stop();
		delete server;
		_log.Log(LOG_STATUS, "udp service is stop");
	}

	if (gwNum == 0 && m_whoIsThread){
		m_whoIsThread->interrupt();
		m_whoIsThread->join();
		m_whoIsThread.reset();
		_log.Log(LOG_STATUS, "whois service is stop");
	}
}

bool XiaomiGateway::StartHardware()
{
	RequestStart();
	m_devInfoInited = false;
	auto result =  m_sql.safe_query
					("SELECT Password, Address FROM Hardware WHERE Type=%d AND ID=%d AND Enabled=1",
					HTYPE_XiaomiGateway, m_HwdID);

	if (result.empty()){
		_log.Log(LOG_ERROR, "Not find or not enable the hardware(type:%d, id=%d)",
							HTYPE_XiaomiGateway, m_HwdID);
		return false;
	}

	m_gwPassword = result[0][0].c_str();
	m_gwIp = result[0][1].c_str();
	addGatewayToList();
	TokenManager::getInstance();

	result = m_sql.safe_query("SELECT Value FROM UserVariables WHERE (name == 'XiaomiMessage')");
	if (!result.empty()){
		m_outputMessage = true;
	}

	result  = m_sql.safe_query ("select Model, Mac from DeviceStatus where HardwareID == %d", m_HwdID);
	for (const auto& itt : result){

		/* device already at map */
		if (getDevice(itt[1])){
			continue;
		}

		const DevAttr* attr = XiaomiGateway::findDevAttr(itt[0]);
		if (nullptr == attr){
			_log.Log(LOG_ERROR, "no support the device, model:%s mac:%s", itt[0].c_str(), itt[1].c_str());
			m_sql.safe_query ("delete from DeviceStatus where HardwareID == %d and Mac =='%q'",
				m_HwdID, itt[1].c_str());
			continue;
		}

		std::shared_ptr<Device> dev(new Device (itt[1], attr));
		if (dev){
			XiaomiGateway::addDeviceToMap(itt[1], dev);
		}
		else{
			_log.Log(LOG_ERROR, "new a device error, so interesting");
		}
	}

	m_thread = std::shared_ptr<std::thread>(new std::thread(&XiaomiGateway::Do_Work, this));
	if (nullptr == m_thread){
		return false;
	}

	SetThreadNameInt(m_thread->native_handle());
	return true;
}

bool XiaomiGateway::StopHardware()
{
	if (m_thread){
		RequestStop();
		m_thread->join();
		m_thread.reset();
	}
	rmFromGatewayList();
	clearDeviceFromMap();
	m_outputMessage = false;
	m_devInfoInited = false;
	m_gwSid = "";
	m_gwModel = "";
	m_gwIp = "";
	m_gwUnicastPort = 0;
	m_localIp = "";
	m_gwPassword = "";
	_log.Log(LOG_STATUS, "stop the XiaomiGateway Hardware");
	return true;
}


int XiaomiGateway::getLocalIpAddr(std::vector<std::string>& ipAddrs)
{
#ifdef WIN32
	return 0;
#else
	struct ifaddrs *myaddrs;
	struct ifaddrs *ifa;

	void *in_addr;
	char buf[64];
	int count = 0;

	if (getifaddrs(&myaddrs) != 0){
		_log.Log(LOG_ERROR, "getifaddrs failed! (when trying to determine local ip address)");
		perror("getifaddrs");
		return 0;
	}

	for (ifa = myaddrs; ifa != NULL; ifa = ifa->ifa_next){
		if (ifa->ifa_addr == NULL  || !(ifa->ifa_flags & IFF_UP)){
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

		if (!inet_ntop(ifa->ifa_addr->sa_family, in_addr, buf, sizeof(buf))){
			_log.Log(LOG_ERROR, "Could not convert to IP address, inet_ntop failed for interface %s", ifa->ifa_name);
		}
		else{
			ipAddrs.push_back(buf);
			count++;
		}
	}

	freeifaddrs(myaddrs);
	return count;
#endif
}


std::string XiaomiGateway::getSimilarLocalAddr(const std::string& gwIp)
{
	std::string ip;
	try{
		/* There is a deep meaning in this, to distinguish has forgotten */
		std::string compareIp = gwIp.substr(0, (gwIp.length() - 3));
		std::vector<std::string> ipAddrs;
		if (getLocalIpAddr(ipAddrs) <= 0){
			_log.Log(LOG_STATUS, "XiaomiGateway (ID=%d): Could not find local IP address with ifaddrs", m_HwdID);
			return "";
		}

		for (const std::string &addr : ipAddrs){
			std::size_t found = addr.find(compareIp);
			if (found != std::string::npos){
				ip = addr;
				_log.Log(LOG_STATUS, "XiaomiGateway (ID=%d): Using %s for local IP address.",
						m_HwdID, ip.c_str());
				break;
			}
		}
	}

	catch (std::exception& e){
		_log.Log(LOG_ERROR, "XiaomiGateway (ID=%d): Could not find local IP address with ifaddrs: %s", m_HwdID, e.what());
	}

	return ip;
}

void XiaomiGateway::Do_Work()
{
	{
		m_localIp = getSimilarLocalAddr(m_gwIp);
		std::unique_lock<std::mutex> lock(UdpServer::m_mutex);
		if (!UdpServer::m_isRun)
		{
			UdpServer* server = new UdpServer (m_localIp);
			if (nullptr == server){
				StopHardware();
				_log.Log(LOG_ERROR, "init upd server failed");
				return;
			}
			bool res = server->run();
			if (false == res){
				StopHardware();
				_log.Log(LOG_ERROR, "init upd server failed");
				return;
			}
			UdpServer::setUdpServer(server);
			UdpServer::m_isRun = true;
			_log.Log(LOG_STATUS, "XiaomiGateway: udp server is start...");
		}

		sendMessageToGateway("{\"cmd\":\"whois\"}", "224.0.0.50", 4321);

		if (!m_whoIsThread){

			m_whoIsThread = std::make_shared<boost::thread>(XiaomiGateway::whois);
			SetThreadName(m_whoIsThread->native_handle(), "XiaomiGwWhois");
		}
	}
	_log.Log(LOG_STATUS, "XiaomiGateway (ID=%d): Worker started...", m_HwdID);

	int sec_counter = 0;
	bool timeout;
	while (!IsStopRequested(1000))
	{
		sec_counter++;
		if ((sec_counter % 12) == 0){
			m_LastHeartbeat = mytime(NULL);
		}

		if ((sec_counter % 60) != 0){
			continue;
		}
		std::unique_lock<std::mutex> lock(m_mutex);
		std::shared_ptr<Device> dev;
		for (const auto & itt : m_deviceMap)
		{
			dev = itt.second;
			if (!dev){
				_log.Log(LOG_ERROR, "The device map second null, so interesting."
									" mac:%s", itt.first.c_str());
				continue;
			}
			timeout = dev->checkTimeout(m_LastHeartbeat);
			if (true == timeout)
			{
				setOnlineStatus(dev, false);
				_log.Log(LOG_STATUS, "have device offline, timeout for %d min,"
										" model:%s	mac:%s",
										dev->getTimeoutLevel() / 60,
										dev->getZigbeeModel().c_str(),
										dev->getMac().c_str());
			}
		}
	}
	_log.Log(LOG_STATUS, "XiaomiGateway (ID=%d): stopped", m_HwdID);
}

std::string XiaomiGateway::getGatewayIp()
{
	return m_gwIp;
}


XiaomiGateway * XiaomiGateway::getGatewayByIp(std::string ip)
{
	XiaomiGateway * ret = NULL;
	{
		std::unique_lock<std::mutex> lock(m_gwListMutex);
		for (const auto& itt : m_gwList){
			if (itt->getGatewayIp() == ip){
				_log.Debug(DEBUG_HARDWARE, "XiaomiGateway: find a gateway ip:%s at list", ip.c_str());
				ret = itt;
				break;
			}
		}
	}
	return ret;
}

int XiaomiGateway::gatewayListSize()
{
	int size = 0;
	{
		std::unique_lock<std::mutex> lock(m_gwListMutex);
		size = m_gwList.size();
	}
	return size;
}

void XiaomiGateway::addGatewayToList()
{
	std::unique_lock<std::mutex> lock(m_gwListMutex);
	auto it = std::find(m_gwList.begin(), m_gwList.end(), this);
	if (it == m_gwList.end()){
		m_gwList.push_back(this);
	}
}

void XiaomiGateway::rmFromGatewayList()
{
	std::unique_lock<std::mutex> lock(m_gwListMutex);
	m_gwList.remove(this);
}

void XiaomiGateway::initDeviceAttrMap(const DevInfo devInfo[], int size)
{
	int ii;
	for (ii = 0; ii < size; ii++){
		m_attrMap.emplace(devInfo[ii].zigbeeModel, new DevAttr(devInfo[ii]));
	}

	_log.Log(LOG_STATUS, "support device number: %lu", m_attrMap.size());
	for (const auto & itt : m_attrMap){
		_log.Debug(DEBUG_HARDWARE, "zigbee model: %s", itt.first.c_str());
	}
	return;
}

const DevAttr* XiaomiGateway::findDevAttr(const std::string& model)
{

	auto it = m_attrMap.find(model);
	if (it == m_attrMap.end()){
		_log.Log(LOG_STATUS, "error: not support zigbee model: %s", model.c_str());
		return nullptr;
	}
	return it->second;
}

void XiaomiGateway::addDeviceToMap(const std::string& mac, std::shared_ptr<Device> ptr)
{
	if (!ptr){
		_log.Log(LOG_ERROR, "add device to map, device null");
		return;
	}
	std::unique_lock<std::mutex> lock(m_mutex);
	m_deviceMap.emplace(mac, ptr);
	_log.Debug(DEBUG_HARDWARE, "add device to map mac: %s", mac.c_str());
}

void XiaomiGateway::delDeviceFromMap(const std::string& mac)
{
	std::unique_lock<std::mutex> lock(m_mutex);
	m_deviceMap.erase(mac);
	_log.Debug(DEBUG_HARDWARE, "delDeviceFromMap mac: %s", mac.c_str());
}

void XiaomiGateway::clearDeviceFromMap()
{
	std::unique_lock<std::mutex> lock(m_mutex);
	m_deviceMap.clear();
	_log.Debug(DEBUG_HARDWARE, "clear all device in map");
}


std::shared_ptr<Device> XiaomiGateway::getDevice(const std::string& mac)
{
	std::shared_ptr<Device> ptr;
	ptr.reset();
	std::unique_lock<std::mutex> lock(m_mutex);
	auto it = m_deviceMap.find(mac);
	if (it == m_deviceMap.end()){
		_log.Log(LOG_STATUS, "error getDevice failed mac:%s", mac.c_str());
		return ptr;
	}
	return it->second;
}

std::shared_ptr<Device> XiaomiGateway::getDevice(unsigned int ssid, int type, int subType, int unit)
{
	bool res;
	std::shared_ptr<Device> ptr;
	ptr.reset();
	std::unique_lock<std::mutex> lock(m_mutex);

	for (const auto itt : m_deviceMap){
		res = itt.second->match(ssid, type, subType, unit);
		if (true == res){
			ptr = itt.second;
			break;
		}
	}
	if (!ptr){
		_log.Log(LOG_ERROR, "getDevice failed ssid:%x  type:%x  subType:%x  unit:%d", ssid, type, subType, unit);
	}
	return ptr;
}



bool XiaomiGateway::createWriteParam(const char * pdata, int length, WriteParam& param, std::shared_ptr <Device>& dev)
{
	if (pdata == nullptr){
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

		/* gateway */
		case pTypeMannageDevice:
		{
			ssid = TbGateway::idConvert(m_gwSid).first;
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
	param.gwMac = m_gwSid;
	param.miGateway = static_cast<void*>(this);

	dev.reset();
	dev = getDevice(ssid,
					static_cast<int>(packetType),
					static_cast<int>(subType),
					static_cast<int>(unit));
	if (!dev){
		_log.Log(LOG_ERROR, "no find want to write device");
		return false;
	}

	if (packetType == pTypeMannageDevice){
		const tRBUF *xcmd = reinterpret_cast<const tRBUF *>(pdata);
		if (xcmd->MANNAGE.cmnd != cmdRmDevice){
			return true;
		}
		int rmSsid = xcmd->MANNAGE.id1 << 24 | xcmd->MANNAGE.id2 << 16 |
					xcmd->MANNAGE.id3 << 8 | xcmd->MANNAGE.id4;
		int rmType = xcmd->MANNAGE.value1;
		int rmSubType = xcmd->MANNAGE.value2;
		int rmUnit = xcmd->MANNAGE.value3;

		std::shared_ptr <Device> rmDev = getDevice(rmSsid, rmType, rmSubType, rmUnit);

		if (!rmDev){
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

	if (!isDevInfoInited()){
		_log.Log(LOG_ERROR, "DT and zgbd shakehand(get dev list) not finished, please check");
		return false;
	}
	result = createWriteParam(pdata, length, param, dev);
	if(false == result){
		_log.Log(LOG_ERROR, "pdate param check error");
		return false;
	}

#if 0
	if (getOnlineStatus(dev) == OnlineStatus::Offline){
		_log.Log(LOG_STATUS, "device is offline, can't control. model:%s Mac:%s",
							  dev->getZigbeeModel().c_str(), dev->getMac().c_str());
		return false;
	}
#endif

	const tRBUF *pCmd = reinterpret_cast<const tRBUF *>(pdata);
	unsigned char packettype = pCmd->ICMND.packettype;

	if (m_gwSid == ""){
		m_gwSid = TokenManager::getInstance().getSid(m_gwIp);
	}

	result = dev->writeTo(param);
	if (false == result){
		sleep_seconds(1);
		result = dev->writeTo(param);
	}

	if (result && packettype == pTypeMannageDevice ){

		const tRBUF *xcmd = reinterpret_cast<const tRBUF *>(pdata);
		if (xcmd->MANNAGE.cmnd == cmdRmDevice){
			delDeviceFromMap(param.rmMac);
		}
	}
	return result;
}

bool XiaomiGateway:: sendMessageToGateway(const std::string &controlMessage, const std::string ip, int port) {

	if (m_gwUnicastPort == 0 && port == 0){
		_log.Log(LOG_ERROR, "gateway unicast port and param port 0 , please check");
		return false;
	}

	if (m_gwIp.empty() && ip.empty()){
		_log.Log(LOG_ERROR, "gateway unicast ip and param ip empty , please check");
		return false;
	}
	bool result = true;
	std::string message = controlMessage;
	std::string remoteIp = (ip.empty())? m_gwIp : ip ;
	int remotePort = (port != 0)? port : m_gwUnicastPort;

	boost::asio::io_service io_service;
	boost::asio::ip::udp::endpoint local_endpoint (boost::asio::ip::address::from_string("127.0.0.1"), 0);
	boost::asio::ip::udp::endpoint remote_endpoint (boost::asio::ip::address::from_string(remoteIp), remotePort);
	boost::asio::ip::udp::socket socket_(io_service, local_endpoint);

	stdreplace(message, "@gatewaykey", getGatewayKey());
	std::shared_ptr<std::string> sharedMessage(new std::string(message));
	try{
		socket_.send_to(boost::asio::buffer(*sharedMessage), remote_endpoint);
		//sleep_milliseconds(50);
		socket_.close();
	}
	catch (boost::system::system_error &e)
	{
		boost::system::error_code ec = e.code();
		std::cerr << ec.value() << std::endl;
		std::cerr << ec.category().name() << std::endl;
		_log.Log(LOG_ERROR,  "send to GW error");
		return false;
	}

	_log.Log(LOG_STATUS,  "send to GW :%s", message.c_str());
	return result;
}


std::string XiaomiGateway::getGatewayKey()
{
#ifdef WWW_ENABLE_SSL
	const unsigned char *key = (unsigned char *)m_gwPassword.c_str();
	unsigned char iv[AES_BLOCK_SIZE] = { 0x17, 0x99, 0x6d, 0x09, 0x3d, 0x28, 0xdd, 0xb3, 0xba, 0x69, 0x5a, 0x2e, 0x6f, 0x58, 0x56, 0x2e };
	std::string token = TokenManager::getInstance().getToken(m_gwIp);
	unsigned char *plaintext = (unsigned char *)token.c_str();
	unsigned char ciphertext[128];

	AES_KEY encryption_key;
	AES_set_encrypt_key(key, 128, &(encryption_key));
	AES_cbc_encrypt((unsigned char *)plaintext, ciphertext, sizeof(plaintext) * 8, &encryption_key, iv, AES_ENCRYPT);

	char gatewaykey[128];
	for (int i = 0; i < 16; i++){
		sprintf(&gatewaykey[i * 2], "%02X", ciphertext[i]);
	}

#ifdef _DEBUG
	_log.Log(LOG_STATUS, "XiaomiGateway: getGatewayKey Password - %s, token=%s", m_gwPassword.c_str(), token.c_str());
	_log.Log(LOG_STATUS, "XiaomiGateway: getGatewayKey key - %s", gatewaykey);
#endif
	return gatewaykey;
#else
	_log.Log(LOG_ERROR, "XiaomiGateway: getGatewayKey NO SSL AVAILABLE");
	return std::string("");
#endif
}


void XiaomiGateway::updateHardwareInfo(const std::string& model, const std::string& mac)
{
	auto result = m_sql.safe_query("SELECT  Model, Mac FROM Hardware WHERE (ID==%d)", m_HwdID);
	if (result.empty()){
		_log.Log(LOG_ERROR, "not find the hwid:%d at db, so interesting", m_HwdID);
		return;
	}

	std::string oldModel = result[0][0];
	std::string oldMac = result[0][1];
	if (oldModel != "Unknown" && oldMac != "Unknown"){
		return;
	}
	m_sql.safe_query("UPDATE Hardware SET Model='%q', Mac='%q'  WHERE (ID==%d)", model.c_str(), mac.c_str() , m_HwdID);
	_log.Log(LOG_STATUS, "update the hardware model and mac, %s  %s",  model.c_str(),  mac.c_str());
}


bool XiaomiGateway::paramCheckSid(const Json::Value& json)
{
	if (false == json.isMember(keySid)){
		_log.Log(LOG_ERROR,  "sid is absent at the json");
		return false;
	}

	if (false == json[keySid].isString()){
		_log.Log(LOG_ERROR,  "sid is not a string at the json");
		return false;
	}

	std::string sid = json[keySid].asString();
	if (!checkZigbeeMac(sid)){
		_log.Log(LOG_ERROR,  "sid format check error at the json");
		return false;
	}
	return true;
}

bool XiaomiGateway::paramCheckModel(const Json::Value& json)
{
	if (false == json.isMember(keyModel)){
		_log.Log(LOG_ERROR,  "sid is absent at the json");
		return false;
	}
	if (false == json[keyModel].isString()){
		_log.Log(LOG_ERROR,  "sid is not a string at the json");
		return false;
	}
	return true;
}

void XiaomiGateway::sendUnsupportDevCmd(const std::string& sid, const std::string& model)
{
	Json::Value delCmd;
	delCmd[keyCmd] = "write";
	delCmd[keyKey] = "@gatewaykey";
	delCmd[keySid] = m_gwSid;
	delCmd[keyParams][0]["unsupported_device"] = sid;
	std::string message = JSonToRawString (delCmd);
	sendMessageToGateway(message);
	_log.Log(LOG_ERROR, "XiaomiGateway: not support the device"
						" model:%s	mac:%s", sid.c_str(), model.c_str());
}

void XiaomiGateway::sendReadCmd(const std::string& sid)
{
	Json::Value readCmd;
	readCmd[keyCmd] = "read";
	readCmd[keySid] = sid;
	std::string message = JSonToRawString (readCmd);
	sendMessageToGateway(message);
}
void XiaomiGateway::deviceListHandler(const Json::Value& root)
{
	std::string gwSid;
	std::string token;
	std::string sid;
	std::string model;
	std::string message;

	gwSid = root[keySid].asString();
	token = root[keyToken].asString();
	m_gwSid = gwSid;
	if (!getDevice(gwSid)){
		const DevAttr* attr = XiaomiGateway::findDevAttr(m_gwModel);
		if (attr == nullptr){
			_log.Log(LOG_ERROR, "XiaomiGateway: No support the gateway(%s, %s)", m_gwModel.c_str(), gwSid.c_str());
			return;
		}

		std::shared_ptr<Device> dev(new Device (gwSid, attr));
		if (!dev){
			_log.Log(LOG_ERROR, "new dev failed, line: %d", __LINE__);
			return;
		}
		XiaomiGateway::addDeviceToMap(gwSid, dev);
		updateHardwareInfo(m_gwModel, gwSid);
	}
	TokenManager::getInstance().updateTokenSid(m_gwIp, token, gwSid);
	setDevInfoInited(true);

	Json::Value list = root[keyDevList];
	for (int ii = 0; ii < (int)list.size(); ii++){

		if (false == paramCheckSid(list[ii]) ||
			false == paramCheckModel(list[ii])){
			continue;
		}

		sid = list[ii][keySid].asString();
		model = list[ii][keyModel].asString();

		const DevAttr* attr = XiaomiGateway::findDevAttr(model);
		if(nullptr == attr){
			sendUnsupportDevCmd(sid, model);
			continue;
		}

		/* if device not exist */
		if (!getDevice(sid)){
			std::shared_ptr<Device> dev(new Device (sid, attr));
			if (!dev){
				_log.Log(LOG_ERROR, "new a device err sid:%s model:%s", sid.c_str(), model.c_str());
				continue;
			}

			bool res = createDtDevice(dev);
			if (false == res){
				_log.Log(LOG_ERROR, "createDtDevice err sid:%s model:%s", sid.c_str(), model.c_str());
				continue;
			}
			XiaomiGateway::addDeviceToMap(sid, dev);
		}

		std::string cmd = root[keyCmd].asString();
		if(cmd == repNewDevList){
			auto dev = getDevice(sid);
			if (!dev)
			{
				_log.Log(LOG_ERROR, "someting interesting hanppend get device null sid:%s model:%s", sid.c_str(), model.c_str());
				return;
			}
			setOnlineStatus(dev, true);
		}
		else if (cmd == rspDiscorey)
		{
			sendReadCmd(sid);
		}
	}

}

void XiaomiGateway::joinGatewayHandler(const Json::Value& root)
{
	std::string model = root["model"].asString();
	std::string ip = root["ip"].asString();
	std::string port =	root["port"].asString();

	if (ip != getGatewayIp()){
		_log.Log(LOG_ERROR, "a new gateway incoming, but not in domoticz hardware list, ip:%s", ip.c_str());
		return;
	}

	m_gwUnicastPort = std::stoi(port);
	m_gwModel = model;

	const DevAttr* attr = XiaomiGateway::findDevAttr(m_gwModel);
	if(attr == nullptr){
		_log.Log(LOG_ERROR, "No support Gateway model:%s", model.c_str());
		return;
	}

	std::string message = "{\"cmd\" : \"discovery\"}";
	sendMessageToGateway(message);
	_log.Log(LOG_STATUS, "Tenbat Gateway(%s) Detected", model.c_str());
}

void XiaomiGateway::setOnlineStatus(std::shared_ptr<Device>& dev, bool status)
{
	OnlineStatus on = dev->getOnline();

	/* if set online, update the timestamp by the way */
	if (true == status){
		dev->updateTimestamp(m_LastHeartbeat);
	}

	if ((status && on == OnlineStatus::Online) ||
		(!status && on == OnlineStatus::Offline)){
		_log.Debug(DEBUG_HARDWARE, "device online status already is %d", status);
		return;
	}

	dev->setOnline(status);
	std::string mac = dev->getMac();
	auto result = m_sql.safe_query("select Online from DeviceStatus where MAC == '%q'", mac.c_str());
	if (!result.empty()){

		int online = 0;
		try{
			online = std::stoi(result[0][0]);
		}
		catch(std::exception& e){
			_log.Log(LOG_ERROR, "exception: what %s" , e.what());
			return;
		}

		if(status == true && online == 0){
			m_sql.safe_query("update DeviceStatus set Online=%d  where MAC == '%q'",1, mac.c_str());
		}
		else if (status == false && online != 0){
			m_sql.safe_query("update DeviceStatus set Online=%d  where MAC == '%q'",0, mac.c_str());
		}
	}
}



void XiaomiGateway::setOnlineStatus(bool status)
{
	auto dev = getDevice(m_gwSid);
	if (!dev)
	{
		_log.Log(LOG_ERROR, "get gateway manage device failed");
		return;
	}
	setOnlineStatus(dev, status);
}



OnlineStatus XiaomiGateway::getOnlineStatus(std::shared_ptr<Device>& dev)
{
	OnlineStatus sta = dev->getOnline();
	return sta;
}

void XiaomiGateway::whois(void)
{
	_log.Log(LOG_STATUS, "XiaomiGateway:whois thread is running");
	const std::string& msg = "{\"cmd\":\"whois\"}";
	while (1)
	{
		boost::this_thread::sleep(boost::posix_time::seconds(10));
		XiaomiGateway* firstGw = NULL;
		{
			std::unique_lock<std::mutex> lock(m_gwListMutex);
			auto it = m_gwList.begin();
			firstGw = *it;
			if (firstGw && !firstGw->isDevInfoInited())
			{
				firstGw->sendMessageToGateway(msg, "224.0.0.50", 4321);
				_log.Log(LOG_ERROR, "send whois to udp group port 4321");
			}
		}
	}
}


static inline int customImage(int switchType)
{
	int customimage = 0;
	if (customimage == 0)
	{
		if (switchType == static_cast<int>(STYPE_OnOff))
		{
			customimage = 1; //wall socket
		}
		else if (switchType == static_cast<int>(STYPE_Selector))
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


void XiaomiGateway::updateTemperature(const std::string &nodeID, const std::string &name, const float temperature, const int battery)
{
	unsigned int sID = OutletAttr::macToUint(nodeID);
	if (sID > 0) {
		SendTempSensor(sID, battery, temperature, name);
	}
}

void XiaomiGateway::updateHumidity(const std::string &nodeID, const std::string &name, const int humidity, const int battery)
{
	unsigned int sID = OutletAttr::macToUint(nodeID);
	if (sID > 0) {
		SendHumiditySensor(sID, battery, humidity, name);
	}
}

void XiaomiGateway::updatePressure(const std::string &nodeID, const std::string &name, const float pressure, const int battery)
{
	unsigned int sID = OutletAttr::macToUint(nodeID);
	if (sID > 0) {
		SendPressureSensor((sID & 0xff00)>>8, sID & 0Xff, battery, pressure, name);
	}
}

void XiaomiGateway::updateTempHumPressure(const std::string &nodeID, const std::string &name, const std::string& temperature, const std::string& humidity, const std::string& pressure, const int battery)
{
	bool res = true;
	float lastTemp= 0.0;
	int   lastHum = 0;
	int   lastHumStatus = 0;
	int   lastPre = 0;
	int   lastForecast = 0;
	float temp = 0.0;
	int   hum  = 0;
	float pre  = 0.0;
	unsigned int sID = OutletAttr::macToUint(nodeID);
	if (sID == 0){
		return;
	}

	temp = (float)atof(temperature.c_str()) / 100.0f;
	hum  =  atoi(humidity.c_str()) / 100;
	pre  = static_cast<float>(atof(pressure.c_str())) / 100.0f;

	if (temperature == "" || humidity == "" || pressure == ""){
		auto ssid = WeatherOutlet::idConvert(nodeID);
		std::string sValue = "";
		int type    = pTypeTEMP_HUM_BARO;
		int subType = sTypeTHB1;
		int nValue  = 0;
		struct tm updateTime;

		std::cout<<"ssid.first"<<ssid.first<<"ssid.second"<<ssid.second<<std::endl;

		res = m_sql.GetLastValue(m_HwdID, ssid.second.c_str(), 1, type, subType, nValue, sValue, updateTime);
		if(false == res){
			_log.Log(LOG_ERROR, "get TempHumPressure sValue failed");
			return;
		}

		int n = sscanf(sValue.c_str(), "%f;%d;%d;%d;%d",  &lastTemp, &lastHum, &lastHumStatus, &lastPre, &lastForecast);
		if (n == 5){
			if (temperature == ""){
				temp = lastTemp;
			}
			if (humidity == ""){
				hum = lastHum;
			}
			if (pressure == ""){
				pre = lastPre;
			}
		}
		else{
			_log.Log(LOG_STATUS, "get temphumpress sscanf failed string:%s n:%d", sValue.c_str(), n);
		}
	}

	int Forecast = baroForecastNoInfo;
	if (pre < 1000)
		Forecast = baroForecastRain;
	else if (pre < 1020)
		Forecast = baroForecastCloudy;
	else if (pre < 1030)
		Forecast = baroForecastPartlyCloudy;
	else
		Forecast = baroForecastSunny;

	SendTempHumBaroSensor(sID, battery, temp, hum, pre, Forecast, name);

}

void XiaomiGateway::updateTempHum(const std::string &nodeID, const std::string &name, const std::string& temperature, const std::string&  humidity, const int battery)
{
	float lastTemp= 0.0;
	int   lastHum = 0;
	int   lastHumStatus = 0;
	float temp = 0.0;
	int   hum  = 0;
	bool  res = true;

	unsigned int sID = OutletAttr::macToUint(nodeID);
	if (sID == 0){
		return;
	}
	temp = (float)atof(temperature.c_str()) / 100.0f;
	hum =  atoi(humidity.c_str()) / 100;

	if (temperature == "" || humidity == ""){

		auto ssid = WeatherOutlet::idConvert(nodeID);
		std::string sValue = "";
		int type    = pTypeTEMP_HUM;
		int subType = sTypeTH5;
		int nValue  = 0;
		struct tm updateTime;

		res = m_sql.GetLastValue(m_HwdID, ssid.second.c_str(), 1, type, subType, nValue, sValue, updateTime);
		if(false == res){
			_log.Log(LOG_ERROR, "get TempHumPressure sValue failed");
			return;
		}

		int n = sscanf(sValue.c_str(), "%f;%d;%d",  &lastTemp, &lastHum, &lastHumStatus);
		if (n == 3){

			if (temperature == ""){
				temp = lastTemp;
			}
			if (humidity == ""){
				hum = lastHum;
			}
		}
		else{
			_log.Log(LOG_STATUS, "get updateTempHum sscanf failed string:%s n:%d", sValue.c_str(), n);
		}

	}

	SendTempHumSensor(sID, battery, temp, hum, name);
}


void XiaomiGateway::updateRGBLight(const std::string & nodeId, const unsigned char unit, const unsigned char subType,const int onOff, const std::string& brightness, const _tColor&  color, const int battery)
{
	unsigned int sID = OutletAttr::macToUint(nodeId);
	if (sID == 0){
		return;
	}
	int lastLevel = 0;
	int nValue = 0;
	int bright = 0;

	bright = atoi(brightness.c_str());

	std::cout<<"updateRGBLight bright string:"<<brightness<<"  bright:"<<bright<<std::endl;
	std::cout<<"color json:"<<color.toJSONString()<<std::endl;

	std::vector<std::vector<std::string> > result;

	auto ssid = LedOutlet::idConvert(nodeId);
	result = m_sql.safe_query("SELECT nValue, LastLevel FROM DeviceStatus WHERE (HardwareID==%d) AND (DeviceID=='%q') AND (Type==%d)", m_HwdID, ssid.second.c_str(), pTypeColorSwitch);
	if (result.empty())
	{
		_log.Log(LOG_ERROR, "not find the device, mac:%s", nodeId.c_str());
		return;
	}

	nValue = atoi(result[0][0].c_str());
	lastLevel = atoi(result[0][1].c_str());
	bright = (brightness == "")? lastLevel : bright;

	if (nValue == gswitch_sOff && (onOff == Color_LedOff || onOff == Color_SetBrightnessLevel))
	{
		_log.Debug(DEBUG_HARDWARE, "Led off , do not  update LedOff and SetBrightnessLevel status report");
		return;
	}

	if (nValue >=  gswitch_sOn  && onOff == gswitch_sOn && lastLevel == bright)
	{
		_log.Debug(DEBUG_HARDWARE, "The cmd is turn on, but want equal last Level and on");
		return;
	}
	if (onOff == Color_SetBrightnessLevel && lastLevel == bright)
	{
		_log.Debug(DEBUG_HARDWARE, "The cmd is set bright, but want equal last Level");
		return;
	}
	SendRGBWSwitch(sID, unit, subType, onOff, bright, color, battery);

	std::cout<<"nValue:"<<nValue<<"	 lastlevel:"<<lastLevel<<"  bright:"<<bright<<std::endl;
}


void XiaomiGateway::updateRGBGateway(const std::string & nodeID, const std::string & name, const bool bIsOn, const int brightness, const int hue)
{
	if (nodeID.length() < 12) {
		_log.Log(LOG_ERROR, "XiaomiGateway: Node ID %s is too short", nodeID.c_str());
		return;
	}
	std::string str = nodeID.substr(4, 8);
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
	int nValue = 0;
	bool tIsOn = !(bIsOn);
	std::vector<std::vector<std::string> > result;
	result = m_sql.safe_query("SELECT nValue, LastLevel FROM DeviceStatus WHERE (HardwareID==%d) AND (DeviceID=='%q') AND (Type==%d) AND (SubType==%d)", m_HwdID, szDeviceID, pTypeColorSwitch, sTypeColor_RGB_W);

	if (result.empty()){

		_log.Log(LOG_STATUS, "XiaomiGateway: New Gateway Found (%s/%s)", str.c_str(), name.c_str());
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
		m_sql.safe_query("UPDATE DeviceStatus SET name='%q', SwitchType=%d, LastLevel=%d WHERE(HardwareID == %d) AND (DeviceID == '%s') AND (Type == %d)", name.c_str(), static_cast<int>(STYPE_Dimmer), brightness, m_HwdID, szDeviceID, pTypeColorSwitch);

	}
	else {
		nValue = atoi(result[0][0].c_str());
		tIsOn = (nValue != 0);
		lastLevel = atoi(result[0][1].c_str());
		if ((bIsOn != tIsOn) || (brightness != lastLevel)){
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

void XiaomiGateway::updateSwitch(const std::string &nodeID, const std::string &name, const bool bIsOn, const _eSwitchType switchType, const int unit, const int level, const std::string &messageType,  const int battery)
{
	unsigned int sID = OutletAttr::macToUint(nodeID);
	if (sID == 0){
		return;
	}
	_tGeneralSwitch xcmd;
	xcmd.len = sizeof(_tGeneralSwitch) - 1;
	xcmd.id = sID;
	xcmd.type = pTypeGeneralSwitch;
	xcmd.subtype = sSwitchGeneralSwitch;
	xcmd.unitcode = unit;
	xcmd.level = level;

	if (bIsOn){
		xcmd.cmnd = gswitch_sOn;
	}
	else{
		xcmd.cmnd = gswitch_sOff;
	}

	if (switchType == STYPE_Selector){

		xcmd.subtype = sSwitchTypeSelector;
		if (level > 0){
			xcmd.cmnd = gswitch_sSetLevel;
			xcmd.level = level;
		}
	}
	else if (switchType == STYPE_SMOKEDETECTOR){
		xcmd.level = level;
	}
	else if (switchType == STYPE_BlindsPercentage){
		xcmd.level = level;
		xcmd.subtype = sSwitchBlindsT2;
		xcmd.cmnd = gswitch_sSetLevel;
	}

	char szTmp[64];
	sprintf(szTmp, "%08X", (unsigned int)sID);
	std::string ID = szTmp;

	auto result = m_sql.safe_query("SELECT nValue FROM DeviceStatus WHERE (HardwareID!=%d) AND (DeviceID=='%q') AND (Type==%d) AND (Unit == '%d')", m_HwdID, ID.c_str(), xcmd.type, xcmd.unitcode);
	if (!result.empty()){
		_log.Log(LOG_STATUS, "XiaomiGateway: the device is added for another gateway hardware id");
		return;
	}

	result = m_sql.safe_query("SELECT nValue, sValue, batteryLevel FROM DeviceStatus WHERE (HardwareID==%d) AND (DeviceID=='%q') AND (Type==%d) AND (Unit == '%d')", m_HwdID, ID.c_str(), xcmd.type, xcmd.unitcode);
	if (result.empty())
	{
		_log.Log(LOG_ERROR, "XiaomiGateway: the device not in database, mac:%s", nodeID.c_str());
		return ;
	}

	int nValue = atoi(result[0][0].c_str());
	int sValue = atoi(result[0][1].c_str());
	int batteryLevel = atoi(result[0][2].c_str());

	if (messageType == "heartbeat") {
		if (battery != 255) {
			batteryLevel = battery;
			m_sql.safe_query("UPDATE DeviceStatus SET batteryLevel=%d WHERE(HardwareID == %d) AND (DeviceID == '%q') AND (Unit == '%d')", batteryLevel, m_HwdID, ID.c_str(), xcmd.unitcode);
		}
	}
	else {
		if ((bIsOn == false && nValue >= 1) ||
			(bIsOn == true && nValue == 0 && switchType == STYPE_OnOff) ||
			(bIsOn == true && switchType != STYPE_OnOff)) {

			m_mainworker.PushAndWaitRxMessage(this, (const unsigned char *)&xcmd, NULL, batteryLevel);
		}
	}
}

void XiaomiGateway::updateCubeText(const std::string & nodeID, const std::string & name, const std::string &degrees)
{
	unsigned int sID = OutletAttr::macToUint(nodeID);
	if (sID > 0) {
		SendTextSensor(sID>>8, sID & 0xff, 255, degrees.c_str(), name);
	}
}

void XiaomiGateway::updateVoltage(const std::string & nodeID, const std::string & name, const int voltage)
{
	if (voltage < 3600) {
		unsigned int sID = OutletAttr::macToUint(nodeID);
		if (sID > 0) {
			int percent = ((voltage - 2200) / 10);
			float vol = (float)voltage / 1000;
			SendVoltageSensor(sID>>8, sID & 0xff, percent, vol, "Xiaomi Voltage");
		}
	}
}

void XiaomiGateway::updateLux(const std::string & nodeID, const unsigned char unit, const std::string & name, const int illumination, const int battery)
{
	unsigned int sID = OutletAttr::macToUint(nodeID);
	if (sID > 0) {
		float lux = (float)illumination;
		SendLuxSensor(sID, unit, battery, lux, name);
	}
}

void XiaomiGateway::updateKwh(const std::string & nodeID, const std::string & name, const std::string& loadPower, const std::string& consumed, const int battery)
{
	unsigned int sID = OutletAttr::macToUint(nodeID);
	if (sID > 0) {
		if (loadPower != "" || consumed != "")
		{
			double power = atof(loadPower.c_str());
			double consume = atof(consumed.c_str()) / 1000;
			SendKwhMeter(sID>>8 , sID & 0xff, 255, power, consume, name);
		}
	}
}


UdpServer::UdpServer(const std::string &localIp)
	: m_socket(m_ioService, boost::asio::ip::udp::v4())
{
	_log.Log(LOG_STATUS, "UdpServer use localIp:%s", localIp.c_str());
	m_localIp = localIp;
	try {
		m_socket.set_option(boost::asio::ip::udp::socket::reuse_address(true));
		_log.Log(LOG_ERROR, "XiaomiGateway: UdpServer m_localip： %s", UdpServer::m_localIp.c_str());

		if (UdpServer::m_localIp != "") {
			boost::system::error_code ec;
			boost::asio::ip::address listenAddr = boost::asio::ip::address::from_string(UdpServer::m_localIp, ec);
			boost::asio::ip::address mcastAddr = boost::asio::ip::address::from_string("224.0.0.50", ec);
			boost::asio::ip::udp::endpoint listenEndpoint(mcastAddr, 9898);

			m_socket.bind(boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), 9898));
			m_socket.set_option(boost::asio::ip::multicast::join_group(mcastAddr.to_v4(), listenAddr.to_v4()), ec);
			m_socket.bind(listenEndpoint, ec);
		}
		else {
			m_socket.bind(boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), 9898));
			m_socket.set_option(boost::asio::ip::multicast::join_group(boost::asio::ip::address::from_string("224.0.0.50")));
		}
	}
	catch (const boost::system::system_error& ex) {

		_log.Log(LOG_ERROR, "XiaomiGateway: %s", ex.code().category().name());
		return;
	}
	startReceive();
}

UdpServer::~UdpServer()
{
	m_socket.close();
	if (UdpServer::getUdpServer() != nullptr){
		_log.Log(LOG_STATUS, "upd server is still work");
	}
	_log.Log(LOG_STATUS, "~~UdpServer");
	UdpServer::m_isRun = false;
}

void UdpServer::startReceive()
{
	memset(&data_[0], 0, sizeof(data_));
	m_socket.async_receive_from(boost::asio::buffer(data_, max_length), m_remoteEndpoint, boost::bind(&UdpServer::handleReceive, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
}


bool UdpServer::run()
{
	m_serThread = boost::thread(boost::bind(&boost::asio::io_service::run, &m_ioService));
	SetThreadName(m_serThread.native_handle(), "XiaomiGwIO");
	return true;
}

void UdpServer::stop()
{
	std::cout<<"UdpServer stop"<<std::endl;
	m_ioService.stop();
	m_serThread.join();
}



bool UdpServer::recvParamCheck(Json::Value& root)
{
	if (false == root.isMember(keyCmd) ||
		false == root[keyCmd].isString()
		)
	{
		_log.Log(LOG_ERROR, "ParamCheck: Receive the message cmd  not exist or not string");
		return false;
	}

	std::string cmd = root[keyCmd].asString();

	if (cmd.empty() )
	{
		_log.Log(LOG_ERROR, "ParamCheck: Receive the message cmd empty");
		return false;
	}

	/* cmd model */
	if ((cmd == rspRead) ||
		(cmd == rspWrite) ||
		(cmd == repReport) ||
		(cmd == repHBeat))
	{
		if (false == root.isMember(keySid) ||
			false == root.isMember(keyModel)||
			false == root.isMember(keyParams)||
			false == root[keySid].isString() ||
			false == root[keyModel].isString() ||
			false == root[keyParams].isArray())
		{
			_log.Log(LOG_ERROR, "ParamCheck: Receive the message cmd or model params error");
			return false;
		}

		std::string sid = root[keySid].asString();
		std::string model = root[keyModel].asString();
		if (sid.empty() || model.empty())
		{
			_log.Log(LOG_ERROR, "ParamCheck: Receive the message sid or model empty");
			return false;
		}

		if (!XiaomiGateway::checkZigbeeMac(sid))
		{
			_log.Log(LOG_ERROR, "ParamCheck: Device sid(mac:%s) is invalid", sid.c_str());
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


	if (cmd == rspDiscorey ||
		cmd == repNewDevList)
	{
		if (false == root.isMember(keySid) ||
			false == root.isMember(keyToken)||
			false == root.isMember(keyDevList)||
			false == root[keySid].isString() ||
			false == root[keyToken].isString())
/*
		||
			false == root[keyDevList].isArray())
*/
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

		if (!XiaomiGateway::checkZigbeeMac(sid))
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
			false == root.isMember(keyModel)||
			false == root[keyIp].isString() ||
			false == root[keyProtocol].isString() ||
			false == root[keyPort].isString() ||
			false == root[keyModel].isString())
		{
			_log.Log(LOG_ERROR, "ParamCheck: Receive the message miss ip protocal port model");
			return false;
		}

		std::string ip = root[keyIp].asString();
		std::string protocol = root[keyProtocol].asString();
		std::string port = root[keyPort].asString();
		std::string model = root[keyModel].asString();
		if (ip.empty() || protocol.empty() || port.empty() || model.empty())
		{
			_log.Log(LOG_ERROR, "ParamCheck: Receive the message ip protocal port model value null");
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



void UdpServer::handleReceive(const boost::system::error_code & error, std::size_t bytes_recvd)
{

	if (error && error != boost::asio::error::message_size){
		_log.Log(LOG_ERROR, "XiaomiGateway: error in handleReceive %s", error.message().c_str());
		return;
	}

	std::string remoteAddr = m_remoteEndpoint.address().to_v4().to_string();
	XiaomiGateway * gateway = XiaomiGateway::getGatewayByIp(remoteAddr);
	if (!gateway){
		_log.Log(LOG_ERROR, "XiaomiGateway: received data from  unregisted gateway ip:%s!", remoteAddr.c_str());
		startReceive();
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
	if (false == ret || false == root.isObject()){
		_log.Log(LOG_ERROR, "XiaomiGateway: the received data json format error!");
		startReceive();
		return;
	}

	if (!recvParamCheck(root)){
		_log.Log(LOG_ERROR, "XiaomiGateway: param check error!");
		startReceive();
		return;
	}

	Json::Value params;
	std::string cmd;
	std::string model;
	std::string sid;

	cmd = root[keyCmd].asString();
	model = root[keyModel].asString();

	if ((cmd == rspRead) || (cmd == rspWrite) ||
		(cmd == repReport) || (cmd == repHBeat)){

		sid = root[keySid].asString();
		std::shared_ptr <Device> dev = gateway->getDevice(sid);
		if (!dev){
			_log.Log(LOG_ERROR, "XiaomiGateway: not find the device at list! sid:%s", sid.c_str());
			startReceive();
			return;
		}

		if(dev->getZigbeeModel() != model){
			_log.Log(LOG_ERROR, "XiaomiGateway: model not match, expect:%s, but:%s sid:%s",
					dev->getZigbeeModel().c_str(), model.c_str(), sid.c_str());
			startReceive();
			return;
		}

		ReadParam param;
		param.message  = message;
		param.miGateway = static_cast<void*>(gateway);
		dev->recvFrom(param);
		gateway->setOnlineStatus(dev, true);
		gateway->setOnlineStatus(true);

		startReceive();
		return;
	}


	if (cmd == rspDiscorey ||
		cmd == repNewDevList){
		gateway->deviceListHandler(root);
		startReceive();
		return;
	}


	if (cmd == rspWhoIs){
		gateway->joinGatewayHandler(root);
		startReceive();
		return;
	}

	_log.Log(LOG_STATUS, " recv Param Check have some problem, call soft developer cmd:%s", cmd.c_str());
	startReceive();
}


TokenManager& TokenManager::getInstance()
{
	static TokenManager instance;
	return instance;
}

void TokenManager::updateTokenSid(const std::string & ip, const std::string & token, const std::string & sid)
{
	bool found = false;
	std::unique_lock<std::mutex> lock(m_mutex);
	for (unsigned i = 0; i < m_gwTokens.size(); i++) {
		if (boost::get<0>(m_gwTokens[i]) == ip) {
			boost::get<1>(m_gwTokens[i]) = token;
			boost::get<2>(m_gwTokens[i]) = sid;
			found = true;
		}
	}
	if (!found) {
		m_gwTokens.push_back(boost::make_tuple(ip, token, sid));
	}

}

std::string TokenManager::getToken(const std::string & ip)
{
	std::string token = "";
	bool found = false;
	std::unique_lock<std::mutex> lock(m_mutex);
	for (unsigned i = 0; i < m_gwTokens.size(); i++) {
		if (boost::get<0>(m_gwTokens[i]) == ip) {
			token = boost::get<1>(m_gwTokens[i]);
		}
	}
	return token;
}

std::string TokenManager::getSid(const std::string & ip)
{
	std::string sid = "";
	bool found = false;
	std::unique_lock<std::mutex> lock(m_mutex);
	for (unsigned i = 0; i < m_gwTokens.size(); i++) {
		if (boost::get<0>(m_gwTokens[i]) == ip) {
			sid = boost::get<2>(m_gwTokens[i]);
		}
	}
	return sid;
}


namespace http {
	namespace server {
		static std::map< std::string, std::vector<std::string> > sOldList;
		static std::mutex sDevListLock;

		void CWebServer::Cmd_AddZigbeeDevice(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (session.rights != 2){
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}
			std::string hwid = request::findValue(&req, "idx");
			std::string cmd = request::findValue(&req, "command");
			std::string model = request::findValue(&req, "model");

			if (hwid == "" || (cmd != "On" && cmd != "Off")){
				_log.Log(LOG_ERROR, "AddZigbeeDevice command is outof range");
				return;
			}

			int iHardwareID = -1;

			try{
				iHardwareID = std::stoi(hwid);
			}

			catch(std::exception& e){
				_log.Log(LOG_ERROR, "get  hardware(%s) id failed:%s",hwid.c_str(), e.what());
				return;
			}

			CDomoticzHardwareBase *pHardware = m_mainworker.GetHardware(iHardwareID);
			if (pHardware == nullptr){
				_log.Log(LOG_ERROR, "AddZigbeeDevice hardware is not find");
				return;
			}

			if (pHardware->HwdType != HTYPE_XiaomiGateway){
				_log.Log(LOG_ERROR, "AddZigbeeDevice hardware type is not XiaomiGateway");
				return;
			}
			bool res = m_mainworker.AddZigbeeDevice(hwid, cmd, model);
			if (false == res){
				root["status"] = "ERR";
			}
			else{
				root["status"] = "OK";
				root["title"] = "AddZigbeeDevice";
			}

			std::unique_lock<std::mutex> lock(sDevListLock);
			std::vector<std::vector<std::string> > result;
			result = m_sql.safe_query("SELECT Mac, name, Model FROM DeviceStatus WHERE (HardwareID=%d)", iHardwareID);

			sOldList.clear();
			for (const auto &itt : result){
				sOldList.insert(std::make_pair(itt[0], itt));
			}
		}

		void CWebServer::Cmd_GetNewDevicesList(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (session.rights != 2){
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}

			std::string hwid = request::findValue(&req, "idx");
			if (hwid.empty()){
				_log.Log(LOG_ERROR, "Cmd_GetNewDevicesList idx or random empty");
				return;
			}

			int tmp;
			int iHardwareID = -1;

			try{
				iHardwareID = std::stoi(hwid);
			}

			catch(std::exception& e){
				_log.Log(LOG_ERROR, "get  hardware(%s) id failed:%s",hwid.c_str(), e.what());
				return;
			}

			CDomoticzHardwareBase* hardware = m_mainworker.GetHardware(iHardwareID);
			if (nullptr == hardware){
				_log.Log(LOG_ERROR, "GetNewDevicesList hardware is not find");
				return;
			}

			root["status"] = "OK";
			root["title"] = "GetNewDevicesList";

			std::unique_lock<std::mutex> lock(sDevListLock);
			std::vector<std::vector<std::string> > result;
			result = m_sql.safe_query("SELECT Mac, name, Model FROM DeviceStatus WHERE (HardwareID=%d)", iHardwareID);

			int ii = 0;
			for (const auto &itt : result){

				auto iter = sOldList.find(itt[0]);
				/* no find at old list */
				if (iter == sOldList.end()){
					root["dev_list"][ii]["name"] = itt[1];
					root["dev_list"][ii]["Model"] = itt[2];
					root["dev_list"][ii]["Mac"] = itt[0];
					ii++;
				}
			}
			/* if dev_list null, add null at [0] */
			if (0 == ii){
				root["dev_list"][0];
			}

		}

		}
}
