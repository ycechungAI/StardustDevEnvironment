#pragma once

#include "Common.h"

namespace UAlbertaBot
{

namespace SquadOrderTypes
{
	enum { None, Idle, Attack, Defend, Regroup, Drop, Survey, DestroyNeutral, SquadOrderTypes };
}

class SquadOrder
{
    size_t              _type;
    int                 _radius;
    BWAPI::Position     _position;
    std::string         _status;

public:

	SquadOrder() 
		: _type(SquadOrderTypes::None)
        , _radius(0)
	{
	}

	SquadOrder(int type, BWAPI::Position position, int radius, std::string status = "Default") 
		: _type(type)
		, _position(position)
		, _radius(radius) 
		, _status(status)
	{
	}

	const std::string & getStatus() const 
	{
		return _status;
	}

    const BWAPI::Position & getPosition() const
    {
        return _position;
    }

    const int & getRadius() const
    {
        return _radius;
    }

    const size_t & getType() const
    {
        return _type;
    }

	const char getCharCode() const
	{
		switch (_type)
		{
			case SquadOrderTypes::None:    return '-';
			case SquadOrderTypes::Idle:    return 'I';
			case SquadOrderTypes::Attack:  return 'A';
			case SquadOrderTypes::Defend:  return 'D';
			case SquadOrderTypes::Regroup: return 'R';
			case SquadOrderTypes::Drop:    return 'd';
			case SquadOrderTypes::Survey:  return 'S';
			case SquadOrderTypes::DestroyNeutral:  return 'N';
		}
		return '?';
	}

	// These orders are considered combat orders and are linked to combat-related micro.
	bool isCombatOrder() const
	{
		return
			//_type == SquadOrderTypes::Watch ||
			_type == SquadOrderTypes::Attack ||
			//_type == SquadOrderTypes::OmniAttack ||
			_type == SquadOrderTypes::Defend ||
			//_type == SquadOrderTypes::Hold ||
			_type == SquadOrderTypes::Drop ||
			_type == SquadOrderTypes::DestroyNeutral;
	}

	// These orders use the regrouping mechanism to retreat when facing superior enemies.
	// Combat orders not in this group fight on against any odds.
	bool isRegroupableOrder() const
	{
		return
			//_type == SquadOrderTypes::Watch ||
			_type == SquadOrderTypes::Attack ||
			//_type == SquadOrderTypes::OmniAttack ||
			_type == SquadOrderTypes::Regroup ||
			_type == SquadOrderTypes::Defend ||
			_type == SquadOrderTypes::DestroyNeutral;
	}
};
}