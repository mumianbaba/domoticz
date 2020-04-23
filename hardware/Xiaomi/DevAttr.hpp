#pragma once

#include <iostream>
#include <vector>
#include <string>
#include <boost/tuple/tuple.hpp>
#include <initializer_list>
#include <list>

#include "Outlet.hpp"


namespace XiaoMi{

struct DevInfo{

public:
	std::string name;
	std::string zigbeeModel;
	std::string model;
	std::string vendor;
	std::list<const OutletAttr*> outlet;
};




class DevAttr
{
public:
	DevAttr()
	{

	}

	DevAttr(const DevAttr* dattr);
	DevAttr(const DevInfo& devInfo);
	
	
	DevAttr(std::initializer_list<OutletAttr*> list, std::string name, std::string zigbeeModel, std::string model, std::string vendor);

	friend std::ostream & operator << (std::ostream& out, DevAttr& devAttr);
	friend std::ostream & operator << (std::ostream& out, DevAttr const & devAttr);


public:
	std::string getName() const {return m_name;}
	std::string getZigbeeModel() const {return m_zigbeeModel;}
	std::string getModel() const {return m_model;}
	std::string getVendor() const {return m_vendor;}
	const OutletAttr* getOutlet(int Outlet) const
	{
		if (Outlet >= m_Outlet.size())
		{
			return nullptr;
		}
		return m_Outlet.at(Outlet);
	}
	const std::vector<const OutletAttr*>& getOutlet() const
	{
		return m_Outlet;
	}

	bool match(int type, int subType, int unit) const;

private:
	std::string m_name;
	std::string m_zigbeeModel;
	std::string m_model;
	std::string m_vendor;
	std::vector<const OutletAttr*> m_Outlet; 
};


}

