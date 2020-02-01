/*
 *    sfall
 *    Copyright (C) 2008-2020  The sfall team
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "..\OpcodeContext.h"

#include "Math.h"

namespace sfall
{
namespace script
{

void sf_div(OpcodeContext& ctx) {
	if (ctx.arg(1).rawValue() == 0) {
		ctx.printOpcodeError("%s - division by zero.", ctx.getOpcodeName());
		return;
	}
	if (ctx.arg(0).isFloat() || ctx.arg(1).isFloat()) {
		ctx.setReturn(ctx.arg(0).asFloat() / ctx.arg(1).asFloat());
	} else {
		ctx.setReturn(ctx.arg(0).rawValue() / ctx.arg(1).rawValue()); // unsigned division
	}
}

}
}