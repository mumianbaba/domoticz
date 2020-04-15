#pragma once


#include "Outlet.hpp"



namespace XiaoMi{

class XiaomiGateway;



class OnOffOutlet : public OutletAttr
{

public:
	OnOffOutlet(int unit, int dir, std::initializer_list<Rule_OnOff> list);

public:
	bool recvFrom(std::string& root, void * miGateway)  const override;

	bool writeTo(const unsigned char* packet, int len, std::string& mac, std::string& model, std::string& gwMac, std::string& key, void * miGateway) const override;


private:

	std::vector<boost::tuple<std::string  /* key */, std::string /* value */, bool> > m_rule;

};


class SensorBinOutlet : public OutletAttr
{

public:
	SensorBinOutlet(int unit, int swType, int dir, std::initializer_list<Rule_OnOff> list);

public:
	bool recvFrom(std::string& root, void * miGateway)  const override;

	bool writeTo(const unsigned char* packet, int len, std::string& mac, std::string& model, std::string& gwMac, std::string& key, void * miGateway) const override;


private:

	std::vector<boost::tuple<std::string  /* key */, std::string /* value */, bool> > m_rule;

};



}

