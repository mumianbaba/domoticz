#pragma once

#include <iostream>
#include <vector>
#include <string>
#include <boost/tuple/tuple.hpp>
#include <initializer_list>
#include <memory>
#include <list>

#include "XiaoMiType.hpp"
#include "Device.hpp"
#include "DevAttr.hpp"
#include "Outlet.hpp"

namespace XiaoMi{



class DevID
{

public:
	DevID()
	{

	}
	void initID(std::string mac, std::vector<SsidPair> ssidList) 
	{
		m_mac = mac;
		m_ssidList = ssidList;
	}

	friend std::ostream & operator << (std::ostream& out, DevID& devID);

	std::string getMac() const {return m_mac;}
	const std::vector<SsidPair>& getSsid() const {return m_ssidList;}
	bool match(std::string& mac) const;	
	bool match(unsigned int ssid) const;

private:
	std::string m_mac;
	std::vector<SsidPair> m_ssidList;
};



class Device
{
	public:
		Device( std::string mac, const DevAttr* devAttr);
		~Device() {std::cout<< "~Device"<<std::endl;}
	public:
		virtual bool recvFrom(std::string& msg, void *miGateway);
	
		virtual bool writeTo(const unsigned char* packet, int len, std::string& gwMac, std::string& key, void  * miGateway);

	public:
		friend std::ostream & operator << (std::ostream& out, Device& device);
		bool match(std::string& mac) const;
		bool match(unsigned int ssid, int type, int subType, int unit) const;

		std::string getMac()   const 
		{
			return m_devID.getMac();
		}
		const std::vector<SsidPair>& getSsid() const
		{
			return m_devID.getSsid();
		}
		std::string getName() const
		{
			return m_devAttr->getName();
		}
		std::string getZigbeeModel() const
		{
			return m_devAttr->getZigbeeModel();
		}
		std::string getModel() const
		{
			return m_devAttr->getModel();
		}
		std::string getVendor() const
		{
			return m_devAttr->getVendor();
		}
		const std::vector<const OutletAttr*>& getOutlet() const
		{
			return m_devAttr->getOutlet();
		}
	private:
	
		const DevAttr* m_devAttr;
		DevID	m_devID;

};


}



