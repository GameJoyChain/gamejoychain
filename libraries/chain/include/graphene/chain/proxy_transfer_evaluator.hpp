/*
    Copyright (C) 2018 gjc

    This file is part of gjc-core.

    gjc-core is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    gjc-core is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with gjc-core.  If not, see <http://www.gnu.org/licenses/>.
 */
#pragma once
#include <graphene/chain/protocol/operations.hpp>
#include <graphene/chain/evaluator.hpp>
#include <graphene/chain/database.hpp>

namespace graphene { namespace chain {

   class proxy_transfer_evaluator : public evaluator<proxy_transfer_evaluator>
   {
     public:
       typedef proxy_transfer_operation operation_type;

       void_result do_evaluate(const proxy_transfer_operation &o);
       void_result do_apply(const proxy_transfer_operation &o, int32_t billed_cpu_time_us = 0);

     private:
       share_type cut_fee(share_type a, uint16_t p);
   };
} }
