/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file vehicle_gui_base.h Functions/classes shared between the different vehicle list GUIs. */

#ifndef VEHICLE_GUI_BASE_H
#define VEHICLE_GUI_BASE_H

#include "cargo_type.h"
#include "timer/timer_game_calendar.h"
#include "economy_type.h"
#include "sortlist_type.h"
#include "vehicle_base.h"
#include "vehiclelist.h"
#include "window_gui.h"
#include "dropdown_type.h"

typedef GUIList<const Vehicle*, std::nullptr_t, CargoType> GUIVehicleList;

struct GUIVehicleGroup {
	VehicleList::const_iterator vehicles_begin;    ///< Pointer to beginning element of this vehicle group.
	VehicleList::const_iterator vehicles_end;      ///< Pointer to past-the-end element of this vehicle group.

	GUIVehicleGroup(VehicleList::const_iterator vehicles_begin, VehicleList::const_iterator vehicles_end)
		: vehicles_begin(vehicles_begin), vehicles_end(vehicles_end) {}

	std::ptrdiff_t NumVehicles() const
	{
		return std::distance(this->vehicles_begin, this->vehicles_end);
	}

	const Vehicle *GetSingleVehicle() const
	{
		assert(this->NumVehicles() == 1);
		return this->vehicles_begin[0];
	}

	Money GetDisplayProfitThisYear() const
	{
		return std::accumulate(this->vehicles_begin, this->vehicles_end, (Money)0, [](Money acc, const Vehicle *v) {
			return acc + v->GetDisplayProfitThisYear();
		});
	}

	Money GetDisplayProfitLastYear() const
	{
		return std::accumulate(this->vehicles_begin, this->vehicles_end, (Money)0, [](Money acc, const Vehicle *v) {
			return acc + v->GetDisplayProfitLastYear();
		});
	}

	TimerGameEconomy::Date GetOldestVehicleAge() const
	{
		const Vehicle *oldest = *std::max_element(this->vehicles_begin, this->vehicles_end, [](const Vehicle *v_a, const Vehicle *v_b) {
			return v_a->economy_age < v_b->economy_age;
		});
		return oldest->economy_age;
	}
};

typedef GUIList<GUIVehicleGroup, std::nullptr_t, CargoType> GUIVehicleGroupList;

struct BaseVehicleListWindow : public Window {

	enum GroupBy : uint8_t {
		GB_NONE,
		GB_SHARED_ORDERS,

		GB_END,
	};

	GroupBy grouping{}; ///< How we want to group the list.
	VehicleList vehicles{}; ///< List of vehicles.  This is the buffer for `vehgroups` to point into; if this is structurally modified, `vehgroups` must be rebuilt.
	GUIVehicleGroupList vehgroups{}; ///< List of (groups of) vehicles.  This stores iterators of `vehicles`, and should be rebuilt if `vehicles` is structurally changed.
	Listing *sorting = nullptr; ///< Pointer to the vehicle type related sorting.
	uint8_t unitnumber_digits = 0; ///< The number of digits of the highest unit number.
	Scrollbar *vscroll = nullptr;
	VehicleListIdentifier vli{}; ///< Identifier of the vehicle list we want to currently show.
	VehicleID vehicle_sel{}; ///< Selected vehicle
	CargoType cargo_filter_criteria{}; ///< Selected cargo filter index
	uint order_arrow_width = 0; ///< Width of the arrow in the small order list.
	CargoTypes used_cargoes{};

	typedef GUIVehicleGroupList::SortFunction VehicleGroupSortFunction;
	typedef GUIVehicleList::SortFunction VehicleIndividualSortFunction;

	enum ActionDropdownItem : uint8_t {
		ADI_REPLACE,
		ADI_SERVICE,
		ADI_DEPOT,
		ADI_ADD_SHARED,
		ADI_REMOVE_ALL,
		ADI_CREATE_GROUP,
	};

	static const StringID vehicle_depot_name[];
	static const std::initializer_list<const StringID> vehicle_group_by_names;
	static const std::initializer_list<const StringID> vehicle_group_none_sorter_names_calendar;
	static const std::initializer_list<const StringID> vehicle_group_none_sorter_names_wallclock;
	static const std::initializer_list<const StringID> vehicle_group_shared_orders_sorter_names_calendar;
	static const std::initializer_list<const StringID> vehicle_group_shared_orders_sorter_names_wallclock;
	static const std::initializer_list<VehicleGroupSortFunction * const> vehicle_group_none_sorter_funcs;
	static const std::initializer_list<VehicleGroupSortFunction * const> vehicle_group_shared_orders_sorter_funcs;

	BaseVehicleListWindow(WindowDesc &desc, const VehicleListIdentifier &vli);

	void OnInit() override;

	void UpdateSortingFromGrouping();

	void DrawVehicleListItems(VehicleID selected_vehicle, int line_height, const Rect &r) const;
	void UpdateVehicleGroupBy(GroupBy group_by);
	void SortVehicleList();
	void BuildVehicleList();
	void SetCargoFilter(uint8_t index);
	void SetCargoFilterArray();
	void FilterVehicleList();
	StringID GetCargoFilterLabel(CargoType cargo_type) const;
	DropDownList BuildCargoDropDownList(bool full) const;
	Dimension GetActionDropdownSize(bool show_autoreplace, bool show_group, bool show_create);
	DropDownList BuildActionDropdownList(bool show_autoreplace, bool show_group, bool show_create);

	std::span<const StringID> GetVehicleSorterNames() const;

	std::span<VehicleGroupSortFunction * const> GetVehicleSorterFuncs() const
	{
		switch (this->grouping) {
			case GB_NONE:
				return vehicle_group_none_sorter_funcs;
			case GB_SHARED_ORDERS:
				return vehicle_group_shared_orders_sorter_funcs;
			default:
				NOT_REACHED();
		}
	}
};

struct CargoIconOverlay {
	int left;
	int right;
	CargoType cargo_type;
	uint cargo_cap;

	constexpr CargoIconOverlay(int left, int right, CargoType cargo_type, uint cargo_cap)
		: left(left), right(right), cargo_type(cargo_type), cargo_cap(cargo_cap)
	{ }
};

bool ShowCargoIconOverlay();
void AddCargoIconOverlay(std::vector<CargoIconOverlay> &overlays, int x, int width, const Vehicle *v);
void DrawCargoIconOverlay(int x, int y, CargoType cargo_type);
void DrawCargoIconOverlays(std::span<const CargoIconOverlay> overlays, int y);

uint GetVehicleListHeight(VehicleType type, uint divisor = 1);

struct Sorting {
	Listing aircraft;
	Listing roadveh;
	Listing ship;
	Listing train;
};

extern std::array<std::array<BaseVehicleListWindow::GroupBy, VEH_COMPANY_END>, VLT_END> _grouping;
extern std::array<Sorting, BaseVehicleListWindow::GB_END> _sorting;

#endif /* VEHICLE_GUI_BASE_H */
