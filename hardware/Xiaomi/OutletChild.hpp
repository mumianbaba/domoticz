#pragma once

#include <functional>
#include <vector>
#include <list>
#include <map>

#include "Outlet.hpp"



namespace XiaoMi{

class XiaomiGateway;



class OnOffOutlet : public OutletAttr
{

public:
	OnOffOutlet(int unit, int dir, std::initializer_list<RuleOnOff> list);

public:
	bool recvFrom(std::string& root, void * miGateway)  const override;

	bool writeTo(const unsigned char* packet, int len, std::string& mac, std::string& model, std::string& gwMac, std::string& key, void * miGateway) const override;


private:

	std::vector<boost::tuple<std::string  /* key */, std::string /* value */, bool> > m_rule;

};


class SensorBinOutlet : public OutletAttr
{

public:
	SensorBinOutlet(int unit, int swType, int dir, std::initializer_list<RuleOnOff> list);

public:
	bool recvFrom(std::string& root, void * miGateway)  const override;

	bool writeTo(const unsigned char* packet, int len, std::string& mac, std::string& model, std::string& gwMac, std::string& key, void * miGateway) const override;


private:

	std::vector<boost::tuple<std::string  /* key */, std::string /* value */, bool> > m_rule;

};

class KwhOutlet : public OutletAttr
{

public:
	KwhOutlet(int unit, int dir, std::initializer_list<RuleOnOff> list);

public:
	bool recvFrom(std::string& root, void * miGateway)  const override;

	bool writeTo(const unsigned char* packet, int len, std::string& mac, std::string& model, std::string& gwMac, std::string& key, void * miGateway) const override;


private:

	std::vector<boost::tuple<std::string  /* key */, std::string /* value */,  int>> m_rule;

};


class SelectorOutlet : public OutletAttr
{

public:
	SelectorOutlet(int unit, int dir, std::string opts, std::initializer_list<RuleSelector> list);

public:
	bool recvFrom(std::string& root, void * miGateway)  const override;

	bool writeTo(const unsigned char* packet, int len, std::string& mac, std::string& model, std::string& gwMac, std::string& key, void * miGateway) const override;


private:

	std::vector<boost::tuple<std::string  /* key */, std::string /* value */,  int>> m_rule;

};





enum class WeatherType{
	WeatherTemp,
	WeatherHum,
	WeatherTHum,
	WeatherTHBaro,
};


class WeatherOutlet : public OutletAttr
{

public:
	WeatherOutlet(int unit, int dir, WeatherType type, std::initializer_list<RuleWeather> list);

public:
	bool recvFrom(std::string& root, void * miGateway)  const override;

	bool writeTo(const unsigned char* packet, int len, std::string& mac, std::string& model, std::string& gwMac, std::string& key, void * miGateway) const override;

	SsidPair idConverter(const std::string& mac) const override;

public:

	int typeConvert(WeatherType type);
	int subTypeConvert(WeatherType type);

	static SsidPair idConvert(const std::string& mac);

private:

	std::vector<boost::tuple<std::string  /* key */, std::string /* value */,  int>> m_rule;
	WeatherType m_type;

};



enum class LedType{
	LedTemp,
	LedRGB,
	LedRGBTemp,
};


class LedOutlet : public OutletAttr
{

public:
	LedOutlet(int unit, int dir, LedType type, std::initializer_list<RuleLed> list);

public:
	bool recvFrom(std::string& root, void * miGateway)  const override;

	bool writeTo(const unsigned char* packet, int len, std::string& mac, std::string& model, std::string& gwMac, std::string& key, void * miGateway) const override;

public:

	int typeConvert(LedType type);
	int subTypeConvert(LedType type);

	static SsidPair idConvert(const std::string& mac);

private:

	std::vector<boost::tuple<std::string  /* key */, std::string /* value */,  int>> m_rule;
	LedType m_type;

};



}

