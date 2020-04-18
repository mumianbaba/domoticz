#pragma once

#include <iostream>
#include <vector>
#include <string>
#include <boost/tuple/tuple.hpp>
#include <initializer_list>


#include "XiaoMiType.hpp"

namespace XiaoMi{


class OutletAttr
{
public:
	OutletAttr(int type, int subType, int swType, int unit, int direction, std::string opts);
	~OutletAttr(){ std::cout<< "~OutletAttr"<<std::endl;}	

	friend std::ostream & operator << (std::ostream& out, OutletAttr& outletAttr);
	friend std::ostream & operator << (std::ostream& out, OutletAttr* outletAttr);

public:
	virtual bool recvFrom(std::string& root, void * miGateway) const = 0;
	virtual bool writeTo(const unsigned char* packet, int len, std::string& mac, std::string& model, std::string& gwMac, std::string& key, void * miGateway) const = 0;
	virtual SsidPair idConverter(const std::string& mac) const;

public:
	int getType() const {return m_type;}

	int getSubType() const {return m_subType;}

	int getSWType() const {return m_swType;}

	int getUnit() const {return m_unit;}

	int getDirection() const {return m_direction;}

	std::string getOpts() const {return m_opts;}

	bool match(int type, int subType, int unit) const;

	static unsigned int macToUint(const std::string& mac);	

protected:


private:
	const int m_type;
	const int m_subType;
	const int m_swType;
	const int m_unit;
	const int m_direction;
	const std::string m_opts;
};


}



