/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.
 *
 * The MIT License
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <boost/test/unit_test.hpp>

#include <graphene/chain/database.hpp>
#include <graphene/chain/exceptions.hpp>
#include <graphene/chain/hardfork.hpp>

#include <graphene/chain/account_object.hpp>
#include <graphene/chain/asset_object.hpp>
#include <graphene/chain/committee_member_object.hpp>
#include <graphene/chain/market_object.hpp>
#include <graphene/chain/vesting_balance_object.hpp>
#include <graphene/chain/withdraw_permission_object.hpp>
#include <graphene/chain/witness_object.hpp>

#include <fc/crypto/digest.hpp>

#include "../common/database_fixture.hpp"

using namespace graphene::chain;
using namespace graphene::chain::test;

BOOST_FIXTURE_TEST_SUITE( operation_tests, database_fixture )

/**
 * Black swan occurs when price feed falls, triggered by settlement
 * order.
 */
BOOST_AUTO_TEST_CASE( create_account_test )
{
   try {
      trx.operations.push_back(make_account());
      account_create_operation op = trx.operations.back().get<account_create_operation>();

      REQUIRE_THROW_WITH_VALUE(op, registrar, account_id_type(9999999));
      REQUIRE_THROW_WITH_VALUE(op, fee, asset(-1));
      REQUIRE_THROW_WITH_VALUE(op, name, "!");
      REQUIRE_THROW_WITH_VALUE(op, name, "Sam");
      REQUIRE_THROW_WITH_VALUE(op, name, "saM");
      REQUIRE_THROW_WITH_VALUE(op, name, "sAm");
      REQUIRE_THROW_WITH_VALUE(op, name, "6j");
      REQUIRE_THROW_WITH_VALUE(op, name, "j-");
      REQUIRE_THROW_WITH_VALUE(op, name, "-j");
      REQUIRE_THROW_WITH_VALUE(op, name, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
      REQUIRE_THROW_WITH_VALUE(op, name, "aaaa.");
      REQUIRE_THROW_WITH_VALUE(op, name, ".aaaa");
      REQUIRE_THROW_WITH_VALUE(op, options.voting_account, account_id_type(999999999));

      auto auth_bak = op.owner;
      op.owner.add_authority(account_id_type(9999999999), 10);
      trx.operations.back() = op;
      op.owner = auth_bak;
      GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx, ~0 ), fc::exception);
      op.owner = auth_bak;

      trx.operations.back() = op;
      sign( trx,  init_account_priv_key );
      trx.validate();
      PUSH_TX( db, trx, ~0 );

      const account_object& nathan_account = *db.get_index_type<account_index>().indices().get<by_name>().find("nathan");
      BOOST_CHECK(nathan_account.id.space() == protocol_ids);
      BOOST_CHECK(nathan_account.id.type() == account_object_type);
      BOOST_CHECK(nathan_account.name == "nathan");

      BOOST_REQUIRE(nathan_account.owner.num_auths() == 1);
      BOOST_CHECK(nathan_account.owner.key_auths.at(committee_key) == 123);
      BOOST_REQUIRE(nathan_account.active.num_auths() == 1);
      BOOST_CHECK(nathan_account.active.key_auths.at(committee_key) == 321);
      BOOST_CHECK(nathan_account.options.voting_account == GRAPHENE_PROXY_TO_SELF_ACCOUNT);
      BOOST_CHECK(nathan_account.options.memo_key == committee_key);

      const account_statistics_object& statistics = nathan_account.statistics(db);
      BOOST_CHECK(statistics.id.space() == implementation_ids);
      BOOST_CHECK(statistics.id.type() == impl_account_statistics_object_type);
   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( update_account )
{
   try {
      const account_object& nathan = create_account("nathan", init_account_pub_key);
      const fc::ecc::private_key nathan_new_key = fc::ecc::private_key::generate();
      const public_key_type key_id = nathan_new_key.get_public_key();
      const auto& active_committee_members = db.get_global_properties().active_committee_members;

      transfer(account_id_type()(db), nathan, asset(1000000000));

      trx.operations.clear();
      account_update_operation op;
      op.account = nathan.id;
      op.owner = authority(2, key_id, 1, init_account_pub_key, 1);
      op.active = authority(2, key_id, 1, init_account_pub_key, 1);
      op.new_options = nathan.options;
      op.new_options->votes = flat_set<vote_id_type>({active_committee_members[0](db).vote_id, active_committee_members[5](db).vote_id});
      op.new_options->num_committee = 2;
      trx.operations.push_back(op);
      BOOST_TEST_MESSAGE( "Updating account" );
      PUSH_TX( db, trx, ~0 );

      BOOST_CHECK(nathan.options.memo_key == init_account_pub_key);
      BOOST_CHECK(nathan.active.weight_threshold == 2);
      BOOST_CHECK(nathan.active.num_auths() == 2);
      BOOST_CHECK(nathan.active.key_auths.at(key_id) == 1);
      BOOST_CHECK(nathan.active.key_auths.at(init_account_pub_key) == 1);
      BOOST_CHECK(nathan.owner.weight_threshold == 2);
      BOOST_CHECK(nathan.owner.num_auths() == 2);
      BOOST_CHECK(nathan.owner.key_auths.at(key_id) == 1);
      BOOST_CHECK(nathan.owner.key_auths.at(init_account_pub_key) == 1);
      BOOST_CHECK(nathan.options.votes.size() == 2);

      enable_fees();
      {
         account_upgrade_operation op;
         op.account_to_upgrade = nathan.id;
         op.upgrade_to_lifetime_member = true;
         op.fee = db.get_global_properties().parameters.current_fees->calculate_fee(op);
         trx.operations = {op};
         PUSH_TX( db, trx, ~0 );
      }

      BOOST_CHECK( nathan.is_lifetime_member() );
   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( transfer_core_asset )
{
   try {
      INVOKE(create_account_test);

      account_id_type committee_account;
      asset committee_balance = db.get_balance(account_id_type(), asset_id_type());

      const account_object& nathan_account = *db.get_index_type<account_index>().indices().get<by_name>().find("nathan");
      transfer_operation top;
      top.from = committee_account;
      top.to = nathan_account.id;
      top.amount = asset( 10000);
      trx.operations.push_back(top);
      for( auto& op : trx.operations ) db.current_fee_schedule().set_fee(op);

      asset fee = trx.operations.front().get<transfer_operation>().fee;
      trx.validate();
      PUSH_TX( db, trx, ~0 );

      BOOST_CHECK_EQUAL(get_balance(account_id_type()(db), asset_id_type()(db)),
                        (committee_balance.amount - 10000 - fee.amount).value);
      committee_balance = db.get_balance(account_id_type(), asset_id_type());

      BOOST_CHECK_EQUAL(get_balance(nathan_account, asset_id_type()(db)), 10000);

      trx = signed_transaction();
      top.from = nathan_account.id;
      top.to = committee_account;
      top.amount = asset(2000);
      trx.operations.push_back(top);

      for( auto& op : trx.operations ) db.current_fee_schedule().set_fee(op);

      fee = trx.operations.front().get<transfer_operation>().fee;
      set_expiration( db, trx );
      trx.validate();
      PUSH_TX( db, trx, ~0 );

      BOOST_CHECK_EQUAL(get_balance(nathan_account, asset_id_type()(db)), 8000 - fee.amount.value);
      BOOST_CHECK_EQUAL(get_balance(account_id_type()(db), asset_id_type()(db)), committee_balance.amount.value + 2000);

   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( create_committee_member )
{
   try {
      committee_member_create_operation op;
      op.committee_member_account = account_id_type();
      op.fee = asset();
      trx.operations.push_back(op);

      REQUIRE_THROW_WITH_VALUE(op, committee_member_account, account_id_type(99999999));
      REQUIRE_THROW_WITH_VALUE(op, fee, asset(-600));
      trx.operations.back() = op;

      committee_member_id_type committee_member_id = db.get_index_type<primary_index<simple_index<committee_member_object>>>().get_next_id();
      PUSH_TX( db, trx, ~0 );
      const committee_member_object& d = committee_member_id(db);

      BOOST_CHECK(d.committee_member_account == account_id_type());
   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( create_uia )
{
   try {
      asset_id_type test_asset_id = db.get_index<asset_object>().get_next_id();
      asset_create_operation creator;
      creator.issuer = account_id_type();
      creator.fee = asset();
      creator.symbol = "TEST";
      creator.common_options.max_supply = 100000000;
      creator.precision = 2;
      creator.common_options.market_fee_percent = GRAPHENE_MAX_MARKET_FEE_PERCENT/100; /*1%*/
      creator.common_options.issuer_permissions = UIA_ASSET_ISSUER_PERMISSION_MASK;
      creator.common_options.flags = charge_market_fee;
      creator.common_options.core_exchange_rate = price({asset(2),asset(1,asset_id_type(1))});
      trx.operations.push_back(std::move(creator));
      PUSH_TX( db, trx, ~0 );

      const asset_object& test_asset = test_asset_id(db);
      BOOST_CHECK(test_asset.symbol == "TEST");
      BOOST_CHECK(asset(1, test_asset_id) * test_asset.options.core_exchange_rate == asset(2));
      BOOST_CHECK((test_asset.options.flags & white_list) == 0);
      BOOST_CHECK(test_asset.options.max_supply == 100000000);
      BOOST_CHECK(!test_asset.bitasset_data_id.valid());
      BOOST_CHECK(test_asset.options.market_fee_percent == GRAPHENE_MAX_MARKET_FEE_PERCENT/100);
      GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx, ~0 ), fc::exception);

      const asset_dynamic_data_object& test_asset_dynamic_data = test_asset.dynamic_asset_data_id(db);
      BOOST_CHECK(test_asset_dynamic_data.current_supply == 0);
      BOOST_CHECK(test_asset_dynamic_data.accumulated_fees == 0);
      BOOST_CHECK(test_asset_dynamic_data.fee_pool == 0);

      auto op = trx.operations.back().get<asset_create_operation>();
      op.symbol = "TESTFAIL";
      REQUIRE_THROW_WITH_VALUE(op, issuer, account_id_type(99999999));
      REQUIRE_THROW_WITH_VALUE(op, common_options.max_supply, -1);
      REQUIRE_THROW_WITH_VALUE(op, common_options.max_supply, 0);
      REQUIRE_THROW_WITH_VALUE(op, symbol, "A");
      REQUIRE_THROW_WITH_VALUE(op, symbol, "qqq");
      REQUIRE_THROW_WITH_VALUE(op, symbol, "11");
      REQUIRE_THROW_WITH_VALUE(op, symbol, ".AAA");
      REQUIRE_THROW_WITH_VALUE(op, symbol, "AAA.");
      REQUIRE_THROW_WITH_VALUE(op, symbol, "AB CD");
      REQUIRE_THROW_WITH_VALUE(op, symbol, "ABCDEFGHIJKLMNOPQRSTUVWXYZ");
      REQUIRE_THROW_WITH_VALUE(op, common_options.core_exchange_rate, price({asset(-100), asset(1)}));
      REQUIRE_THROW_WITH_VALUE(op, common_options.core_exchange_rate, price({asset(100),asset(-1)}));
   } catch(fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( update_uia )
{
   using namespace graphene;
   try {
      INVOKE(create_uia);
      const auto& test = get_asset("TEST");
      const auto& nathan = create_account("nathan");

      asset_update_operation op;
      op.issuer = test.issuer;
      op.asset_to_update = test.id;
      op.new_options = test.options;

      trx.operations.push_back(op);

      //Cannot change issuer to same as before
      BOOST_TEST_MESSAGE( "Make sure changing issuer to same as before is forbidden" );
      REQUIRE_THROW_WITH_VALUE(op, new_issuer, test.issuer);

      //Cannot convert to an MIA
      BOOST_TEST_MESSAGE( "Make sure we can't convert UIA to MIA" );
      REQUIRE_THROW_WITH_VALUE(op, new_options.issuer_permissions, ASSET_ISSUER_PERMISSION_MASK);
      REQUIRE_THROW_WITH_VALUE(op, new_options.core_exchange_rate, price(asset(5), asset(5)));

      BOOST_TEST_MESSAGE( "Test updating core_exchange_rate" );
      op.new_options.core_exchange_rate = price(asset(3), test.amount(5));
      trx.operations.back() = op;
      PUSH_TX( db, trx, ~0 );
      REQUIRE_THROW_WITH_VALUE(op, new_options.core_exchange_rate, price());
      op.new_options.core_exchange_rate = test.options.core_exchange_rate;
      op.new_issuer = nathan.id;
      trx.operations.back() = op;
      PUSH_TX( db, trx, ~0 );

      BOOST_TEST_MESSAGE( "Test setting flags" );
      op.issuer = nathan.id;
      op.new_issuer.reset();
      op.new_options.flags = transfer_restricted | white_list;
      trx.operations.back() = op;
      PUSH_TX( db, trx, ~0 );

      BOOST_TEST_MESSAGE( "Disable white_list permission" );
      op.new_options.issuer_permissions = test.options.issuer_permissions & ~white_list;
      trx.operations.back() = op;
      PUSH_TX( db, trx, ~0 );

      BOOST_TEST_MESSAGE( "Can't toggle white_list" );
      REQUIRE_THROW_WITH_VALUE(op, new_options.flags, test.options.flags & ~white_list);

      BOOST_TEST_MESSAGE( "Can toggle transfer_restricted" );
      for( int i=0; i<2; i++ )
      {
         op.new_options.flags = test.options.flags ^ transfer_restricted;
         trx.operations.back() = op;
         PUSH_TX( db, trx, ~0 );
      }

      BOOST_TEST_MESSAGE( "Make sure white_list can't be re-enabled" );
      op.new_options.issuer_permissions = test.options.issuer_permissions;
      op.new_options.flags = test.options.flags;
      BOOST_CHECK(!(test.options.issuer_permissions & white_list));
      REQUIRE_THROW_WITH_VALUE(op, new_options.issuer_permissions, UIA_ASSET_ISSUER_PERMISSION_MASK);

      BOOST_TEST_MESSAGE( "We can change issuer to account_id_type(), but can't do it again" );
      op.new_issuer = account_id_type();
      trx.operations.back() = op;
      PUSH_TX( db, trx, ~0 );
      op.issuer = account_id_type();
      GRAPHENE_REQUIRE_THROW(PUSH_TX( db, trx, ~0 ), fc::exception);
      op.new_issuer.reset();
   } catch(fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( issue_uia )
{
   try {
      INVOKE(create_uia);
      INVOKE(create_account_test);

      const asset_object& test_asset = *db.get_index_type<asset_index>().indices().get<by_symbol>().find("TEST");
      const account_object& nathan_account = *db.get_index_type<account_index>().indices().get<by_name>().find("nathan");

      asset_issue_operation op;
      op.issuer = test_asset.issuer;
      op.asset_to_issue =  test_asset.amount(5000000);
      op.issue_to_account = nathan_account.id;
      trx.operations.push_back(op);

      REQUIRE_THROW_WITH_VALUE(op, asset_to_issue, asset(200));
      REQUIRE_THROW_WITH_VALUE(op, fee, asset(-1));
      REQUIRE_THROW_WITH_VALUE(op, issue_to_account, account_id_type(999999999));

      trx.operations.back() = op;
      PUSH_TX( db, trx, ~0 );

      const asset_dynamic_data_object& test_dynamic_data = test_asset.dynamic_asset_data_id(db);
      BOOST_CHECK_EQUAL(get_balance(nathan_account, test_asset), 5000000);
      BOOST_CHECK(test_dynamic_data.current_supply == 5000000);
      BOOST_CHECK(test_dynamic_data.accumulated_fees == 0);
      BOOST_CHECK(test_dynamic_data.fee_pool == 0);

      PUSH_TX( db, trx, ~0 );

      BOOST_CHECK_EQUAL(get_balance(nathan_account, test_asset), 10000000);
      BOOST_CHECK(test_dynamic_data.current_supply == 10000000);
      BOOST_CHECK(test_dynamic_data.accumulated_fees == 0);
      BOOST_CHECK(test_dynamic_data.fee_pool == 0);
   } catch(fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( transfer_uia )
{
   try {
      INVOKE(issue_uia);

      const asset_object& uia = *db.get_index_type<asset_index>().indices().get<by_symbol>().find("TEST");
      const account_object& nathan = *db.get_index_type<account_index>().indices().get<by_name>().find("nathan");
      const account_object& committee = account_id_type()(db);

      BOOST_CHECK_EQUAL(get_balance(nathan, uia), 10000000);
      transfer_operation top;
      top.from = nathan.id;
      top.to = committee.id;
      top.amount = uia.amount(5000);
      trx.operations.push_back(top);
      BOOST_TEST_MESSAGE( "Transfering 5000 TEST from nathan to committee" );
      PUSH_TX( db, trx, ~0 );
      BOOST_CHECK_EQUAL(get_balance(nathan, uia), 10000000 - 5000);
      BOOST_CHECK_EQUAL(get_balance(committee, uia), 5000);

      PUSH_TX( db, trx, ~0 );
      BOOST_CHECK_EQUAL(get_balance(nathan, uia), 10000000 - 10000);
      BOOST_CHECK_EQUAL(get_balance(committee, uia), 10000);
   } catch(fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}


BOOST_AUTO_TEST_CASE( uia_fees )
{
   try {
      INVOKE( issue_uia );

      enable_fees();

      const asset_object& test_asset = get_asset("TEST");
      const asset_dynamic_data_object& asset_dynamic = test_asset.dynamic_asset_data_id(db);
      const account_object& nathan_account = get_account("nathan");
      const account_object& committee_account = account_id_type()(db);
      const share_type prec = asset::scaled_precision( asset_id_type()(db).precision );

      fund_fee_pool(committee_account, test_asset, 1000*prec);
      BOOST_CHECK(asset_dynamic.fee_pool == 1000*prec);

      transfer_operation op;
      op.fee = test_asset.amount(0);
      op.from = nathan_account.id;
      op.to   = committee_account.id;
      op.amount = test_asset.amount(100);
      op.fee = db.current_fee_schedule().calculate_fee( op, test_asset.options.core_exchange_rate );
      BOOST_CHECK(op.fee.asset_id == test_asset.id);
      asset old_balance = db.get_balance(nathan_account.get_id(), test_asset.get_id());
      asset fee = op.fee;
      BOOST_CHECK(fee.amount > 0);
      asset core_fee = fee*test_asset.options.core_exchange_rate;
      trx.operations.push_back(std::move(op));
      PUSH_TX( db, trx, ~0 );

      BOOST_CHECK_EQUAL(get_balance(nathan_account, test_asset),
                        (old_balance - fee - test_asset.amount(100)).amount.value);
      BOOST_CHECK_EQUAL(get_balance(committee_account, test_asset), 100);
      BOOST_CHECK(asset_dynamic.accumulated_fees == fee.amount);
      BOOST_CHECK(asset_dynamic.fee_pool == 1000*prec - core_fee.amount);

      //Do it again, for good measure.
      PUSH_TX( db, trx, ~0 );
      BOOST_CHECK_EQUAL(get_balance(nathan_account, test_asset),
                        (old_balance - fee - fee - test_asset.amount(200)).amount.value);
      BOOST_CHECK_EQUAL(get_balance(committee_account, test_asset), 200);
      BOOST_CHECK(asset_dynamic.accumulated_fees == fee.amount + fee.amount);
      BOOST_CHECK(asset_dynamic.fee_pool == 1000*prec - core_fee.amount - core_fee.amount);

      op = std::move(trx.operations.back().get<transfer_operation>());
      trx.operations.clear();
      op.amount = asset(20);

      BOOST_CHECK_EQUAL(get_balance(nathan_account, asset_id_type()(db)), 0);
      transfer(committee_account, nathan_account, asset(20));
      BOOST_CHECK_EQUAL(get_balance(nathan_account, asset_id_type()(db)), 20);

      trx.operations.emplace_back(std::move(op));
      PUSH_TX( db, trx, ~0 );

      BOOST_CHECK_EQUAL(get_balance(nathan_account, asset_id_type()(db)), 0);
      BOOST_CHECK_EQUAL(get_balance(nathan_account, test_asset),
                        (old_balance - fee - fee - fee - test_asset.amount(200)).amount.value);
      BOOST_CHECK_EQUAL(get_balance(committee_account, test_asset), 200);
      BOOST_CHECK(asset_dynamic.accumulated_fees == fee.amount.value * 3);
      BOOST_CHECK(asset_dynamic.fee_pool == 1000*prec - core_fee.amount.value * 3);
   } catch (fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( witness_pay_test )
{ try {

   const share_type prec = asset::scaled_precision( asset_id_type()(db).precision );

   // there is an immediate maintenance interval in the first block
   //   which will initialize last_budget_time
   generate_block();

   // Make an account and upgrade it to prime, so that witnesses get some pay
   create_account("nathan", init_account_pub_key);
   transfer(account_id_type()(db), get_account("nathan"), asset(20000*prec));
   transfer(account_id_type()(db), get_account("init3"), asset(20*prec));
   generate_block();

   auto last_witness_vbo_balance = [&]() -> share_type
   {
      const witness_object& wit = db.fetch_block_by_number(db.head_block_num())->witness(db);
      if( !wit.pay_vb.valid() )
         return 0;
      return (*wit.pay_vb)(db).balance.amount;
   };

   const auto block_interval = db.get_global_properties().parameters.block_interval;
   const asset_object* core = &asset_id_type()(db);
   const account_object* nathan = &get_account("nathan");
   enable_fees();
   BOOST_CHECK_GT(db.current_fee_schedule().get<account_upgrade_operation>().membership_lifetime_fee, 0);
   // Based on the size of the reserve fund later in the test, the witness budget will be set to this value
   const uint64_t ref_budget =
      ((uint64_t( db.current_fee_schedule().get<account_upgrade_operation>().membership_lifetime_fee )
         * GRAPHENE_CORE_ASSET_CYCLE_RATE * 30
         * block_interval
       ) + ((uint64_t(1) << GRAPHENE_CORE_ASSET_CYCLE_RATE_BITS)-1)
      ) >> GRAPHENE_CORE_ASSET_CYCLE_RATE_BITS
      ;
   // change this if ref_budget changes
   BOOST_CHECK_EQUAL( ref_budget, 594 );
   const uint64_t witness_ppb = ref_budget * 10 / 23 + 1;
   // change this if ref_budget changes
   BOOST_CHECK_EQUAL( witness_ppb, 259 );
   // following two inequalities need to hold for maximal code coverage
   BOOST_CHECK_LT( witness_ppb * 2, ref_budget );
   BOOST_CHECK_GT( witness_ppb * 3, ref_budget );

   db.modify( db.get_global_properties(), [&]( global_property_object& _gpo )
   {
      _gpo.parameters.witness_pay_per_block = witness_ppb;
   } );

   BOOST_CHECK_EQUAL(core->dynamic_asset_data_id(db).accumulated_fees.value, 0);
   BOOST_TEST_MESSAGE( "Upgrading account" );
   account_upgrade_operation uop;
   uop.account_to_upgrade = nathan->get_id();
   uop.upgrade_to_lifetime_member = true;
   set_expiration( db, trx );
   trx.operations.push_back(uop);
   for( auto& op : trx.operations ) db.current_fee_schedule().set_fee(op);
   trx.validate();
   sign( trx, init_account_priv_key );
   PUSH_TX( db, trx );
   auto pay_fee_time = db.head_block_time().sec_since_epoch();
   trx.clear();
   BOOST_CHECK( get_balance(*nathan, *core) == 20000*prec - account_upgrade_operation::fee_parameters_type().membership_lifetime_fee );;

   generate_block();
   nathan = &get_account("nathan");
   core = &asset_id_type()(db);
   BOOST_CHECK_EQUAL( last_witness_vbo_balance().value, 0 );

   auto schedule_maint = [&]()
   {
      // now we do maintenance
      db.modify( db.get_dynamic_global_properties(), [&]( dynamic_global_property_object& _dpo )
      {
         _dpo.next_maintenance_time = db.head_block_time() + 1;
      } );
   };
   BOOST_TEST_MESSAGE( "Generating some blocks" );

   // generate some blocks
   while( db.head_block_time().sec_since_epoch() - pay_fee_time < 24 * block_interval )
   {
      generate_block();
      BOOST_CHECK_EQUAL( last_witness_vbo_balance().value, 0 );
   }
   BOOST_CHECK_EQUAL( db.head_block_time().sec_since_epoch() - pay_fee_time, 24 * block_interval );

   schedule_maint();
   // The 80% lifetime referral fee went to the committee account, which burned it. Check that it's here.
   BOOST_CHECK( core->reserved(db).value == 8000*prec );
   generate_block();
   BOOST_CHECK_EQUAL( core->reserved(db).value, 999999406 );
   BOOST_CHECK_EQUAL( db.get_dynamic_global_properties().witness_budget.value, ref_budget );
   // first witness paid from old budget (so no pay)
   BOOST_CHECK_EQUAL( last_witness_vbo_balance().value, 0 );
   // second witness finally gets paid!
   generate_block();
   BOOST_CHECK_EQUAL( last_witness_vbo_balance().value, witness_ppb );
   BOOST_CHECK_EQUAL( db.get_dynamic_global_properties().witness_budget.value, ref_budget - witness_ppb );

   generate_block();
   BOOST_CHECK_EQUAL( last_witness_vbo_balance().value, witness_ppb );
   BOOST_CHECK_EQUAL( db.get_dynamic_global_properties().witness_budget.value, ref_budget - 2 * witness_ppb );

   generate_block();
   BOOST_CHECK_LT( last_witness_vbo_balance().value, witness_ppb );
   BOOST_CHECK_EQUAL( last_witness_vbo_balance().value, ref_budget - 2 * witness_ppb );
   BOOST_CHECK_EQUAL( db.get_dynamic_global_properties().witness_budget.value, 0 );

   generate_block();
   BOOST_CHECK_EQUAL( last_witness_vbo_balance().value, 0 );
   BOOST_CHECK_EQUAL( db.get_dynamic_global_properties().witness_budget.value, 0 );
   BOOST_CHECK_EQUAL(core->reserved(db).value, 999999406 );

} FC_LOG_AND_RETHROW() }

/**
 *  Reserve asset test should make sure that all assets except bitassets
 *  can be burned, and all supplies add up.
 */
BOOST_AUTO_TEST_CASE( reserve_asset_test )
{
   try
   {
      ACTORS((alice)(bob)(sam)(judge));
      const auto& basset = create_bitasset("USDBIT", judge_id);
      const auto& uasset = create_user_issued_asset("TEST");
      const auto& passet = create_prediction_market("PMARK", judge_id);
      const auto& casset = asset_id_type()(db);

      auto reserve_asset = [&]( account_id_type payer, asset amount_to_reserve )
      {
         asset_reserve_operation op;
         op.payer = payer;
         op.amount_to_reserve = amount_to_reserve;
         transaction tx;
         tx.operations.push_back( op );
         set_expiration( db, tx );
         db.push_transaction( tx, database::skip_authority_check | database::skip_tapos_check | database::skip_transaction_signatures );
      } ;

      auto _issue_uia = [&]( const account_object& recipient, asset amount )
      {
         asset_issue_operation op;
         op.issuer = amount.asset_id(db).issuer;
         op.asset_to_issue = amount;
         op.issue_to_account = recipient.id;
         transaction tx;
         tx.operations.push_back( op );
         set_expiration( db, tx );
         db.push_transaction( tx, database::skip_authority_check | database::skip_tapos_check | database::skip_transaction_signatures );
      } ;

      int64_t init_balance = 10000;
      int64_t reserve_amount = 3000;
      share_type initial_reserve;

      BOOST_TEST_MESSAGE( "Test reserve operation on core asset" );
      transfer( committee_account, alice_id, casset.amount( init_balance ) );

      initial_reserve = casset.reserved( db );
      reserve_asset( alice_id, casset.amount( reserve_amount  ) );
      BOOST_CHECK_EQUAL( get_balance( alice, casset ), init_balance - reserve_amount );
      BOOST_CHECK_EQUAL( (casset.reserved( db ) - initial_reserve).value, reserve_amount );
      verify_asset_supplies(db);

      BOOST_TEST_MESSAGE( "Test reserve operation on market issued asset" );
      transfer( committee_account, alice_id, casset.amount( init_balance*100 ) );
      update_feed_producers( basset, {sam.id} );
      price_feed current_feed;
      current_feed.settlement_price = basset.amount( 2 ) / casset.amount(100);
      publish_feed( basset, sam, current_feed );
      borrow( alice_id, basset.amount( init_balance ), casset.amount( 100*init_balance ) );
      BOOST_CHECK_EQUAL( get_balance( alice, basset ), init_balance );

      GRAPHENE_REQUIRE_THROW( reserve_asset( alice_id, basset.amount( reserve_amount ) ), asset_reserve_invalid_on_mia );

      BOOST_TEST_MESSAGE( "Test reserve operation on prediction market asset" );
      transfer( committee_account, alice_id, casset.amount( init_balance ) );
      borrow( alice_id, passet.amount( init_balance ), casset.amount( init_balance ) );
      GRAPHENE_REQUIRE_THROW( reserve_asset( alice_id, passet.amount( reserve_amount ) ), asset_reserve_invalid_on_mia );

      BOOST_TEST_MESSAGE( "Test reserve operation on user issued asset" );
      _issue_uia( alice, uasset.amount( init_balance ) );
      BOOST_CHECK_EQUAL( get_balance( alice, uasset ), init_balance );
      verify_asset_supplies(db);

      BOOST_TEST_MESSAGE( "Reserving asset" );
      initial_reserve = uasset.reserved( db );
      reserve_asset( alice_id, uasset.amount( reserve_amount  ) );
      BOOST_CHECK_EQUAL( get_balance( alice, uasset ), init_balance - reserve_amount );
      BOOST_CHECK_EQUAL( (uasset.reserved( db ) - initial_reserve).value, reserve_amount );
      verify_asset_supplies(db);
   }
   catch (fc::exception& e)
   {
      edump((e.to_detail_string()));
      throw;
   }
}

BOOST_AUTO_TEST_CASE( vesting_balance_create_test )
{ try {
   INVOKE( create_uia );

   const asset_object& core = asset_id_type()(db);
   const asset_object& test_asset = get_asset("TEST");

   vesting_balance_create_operation op;
   op.fee = core.amount( 0 );
   op.creator = account_id_type();
   op.owner = account_id_type();
   op.amount = test_asset.amount( 100 );
   //op.vesting_seconds = 60*60*24;
   op.policy = cdd_vesting_policy_initializer{ 60*60*24 };

   // Fee must be non-negative
   REQUIRE_OP_VALIDATION_SUCCESS( op, fee, core.amount(1) );
   REQUIRE_OP_VALIDATION_SUCCESS( op, fee, core.amount(0) );
   REQUIRE_OP_VALIDATION_FAILURE( op, fee, core.amount(-1) );

   // Amount must be positive
   REQUIRE_OP_VALIDATION_SUCCESS( op, amount, core.amount(1) );
   REQUIRE_OP_VALIDATION_FAILURE( op, amount, core.amount(0) );
   REQUIRE_OP_VALIDATION_FAILURE( op, amount, core.amount(-1) );

   // Setup world state we will need to test actual evaluation
   const account_object& alice_account = create_account("alice");
   const account_object& bob_account = create_account("bob");

   transfer(committee_account(db), alice_account, core.amount(100000));

   op.creator = alice_account.get_id();
   op.owner = alice_account.get_id();

   account_id_type nobody = account_id_type(1234);

   trx.operations.push_back(op);
   // Invalid account_id's
   REQUIRE_THROW_WITH_VALUE( op, creator, nobody );
   REQUIRE_THROW_WITH_VALUE( op,   owner, nobody );

   // Insufficient funds
   REQUIRE_THROW_WITH_VALUE( op, amount, core.amount(999999999) );
   // Alice can fund a bond to herself or to Bob
   op.amount = core.amount( 1000 );
   REQUIRE_OP_EVALUATION_SUCCESS( op, owner, alice_account.get_id() );
   REQUIRE_OP_EVALUATION_SUCCESS( op, owner,   bob_account.get_id() );
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( vesting_balance_withdraw_test )
{ try {
   INVOKE( create_uia );
   // required for head block time
   generate_block();

   const asset_object& core = asset_id_type()(db);
   const asset_object& test_asset = get_asset( "TEST" );

   vesting_balance_withdraw_operation op;
   op.fee = core.amount( 0 );
   op.vesting_balance = vesting_balance_id_type();
   op.owner = account_id_type();
   op.amount = test_asset.amount( 100 );

   // Fee must be non-negative
   REQUIRE_OP_VALIDATION_SUCCESS( op, fee, core.amount(  1 )  );
   REQUIRE_OP_VALIDATION_SUCCESS( op, fee, core.amount(  0 )  );
   REQUIRE_OP_VALIDATION_FAILURE( op, fee, core.amount( -1 ) );

   // Amount must be positive
   REQUIRE_OP_VALIDATION_SUCCESS( op, amount, core.amount(  1 ) );
   REQUIRE_OP_VALIDATION_FAILURE( op, amount, core.amount(  0 ) );
   REQUIRE_OP_VALIDATION_FAILURE( op, amount, core.amount( -1 ) );

   // Setup world state we will need to test actual evaluation
   const account_object& alice_account = create_account( "alice" );
   const account_object& bob_account = create_account( "bob" );

   transfer( committee_account(db), alice_account, core.amount( 1000000 ) );

   auto spin_vbo_clock = [&]( const vesting_balance_object& vbo, uint32_t dt_secs )
   {
      // HACK:  This just modifies the DB creation record to be further
      //    in the past
      db.modify( vbo, [&]( vesting_balance_object& _vbo )
      {
         _vbo.policy.get<cdd_vesting_policy>().coin_seconds_earned_last_update -= dt_secs;
      } );
   };

   auto create_vbo = [&](
      account_id_type creator, account_id_type owner,
      asset amount, uint32_t vesting_seconds, uint32_t elapsed_seconds
      ) -> const vesting_balance_object&
   {
      transaction tx;

      vesting_balance_create_operation create_op;
      create_op.fee = core.amount( 0 );
      create_op.creator = creator;
      create_op.owner = owner;
      create_op.amount = amount;
      create_op.policy = cdd_vesting_policy_initializer(vesting_seconds);
      tx.operations.push_back( create_op );
      set_expiration( db, tx );

      processed_transaction ptx = PUSH_TX( db,  tx, ~0  );
      const vesting_balance_object& vbo = vesting_balance_id_type(
         ptx.operation_results[0].get<object_id_type>())(db);

      if( elapsed_seconds > 0 )
         spin_vbo_clock( vbo, elapsed_seconds );
      return vbo;
   };

   auto top_up = [&]()
   {
      trx.clear();
      transfer( committee_account(db),
         alice_account,
         core.amount( 1000000 - db.get_balance( alice_account, core ).amount )
         );
      FC_ASSERT( db.get_balance( alice_account, core ).amount == 1000000 );
      trx.clear();
      trx.operations.push_back( op );
   };

   trx.clear();
   trx.operations.push_back( op );

   {
      // Try withdrawing a single satoshi
      const vesting_balance_object& vbo = create_vbo(
         alice_account.id, alice_account.id, core.amount( 10000 ), 1000, 0);

      FC_ASSERT( db.get_balance( alice_account,       core ).amount ==  990000 );

      op.vesting_balance = vbo.id;
      op.owner = alice_account.id;

      REQUIRE_THROW_WITH_VALUE( op, amount, core.amount(1) );

      // spin the clock and make sure we can withdraw 1/1000 in 1 second
      spin_vbo_clock( vbo, 1 );
      // Alice shouldn't be able to withdraw 11, it's too much
      REQUIRE_THROW_WITH_VALUE( op, amount, core.amount(11) );
      op.amount = core.amount( 1 );
      // Bob shouldn't be able to withdraw anything
      REQUIRE_THROW_WITH_VALUE( op, owner, bob_account.id );
      // Shouldn't be able to get out different asset than was put in
      REQUIRE_THROW_WITH_VALUE( op, amount, test_asset.amount(1) );
      // Withdraw the max, we are OK...
      REQUIRE_OP_EVALUATION_SUCCESS( op, amount, core.amount(10) );
      FC_ASSERT( db.get_balance( alice_account,       core ).amount ==  990010 );
      top_up();
   }

   // Make sure we can withdraw the correct amount after 999 seconds
   {
      const vesting_balance_object& vbo = create_vbo(
         alice_account.id, alice_account.id, core.amount( 10000 ), 1000, 999);

      FC_ASSERT( db.get_balance( alice_account,       core ).amount ==  990000 );

      op.vesting_balance = vbo.id;
      op.owner = alice_account.id;
      // Withdraw one satoshi too much, no dice
      REQUIRE_THROW_WITH_VALUE( op, amount, core.amount(9991) );
      // Withdraw just the right amount, success!
      REQUIRE_OP_EVALUATION_SUCCESS( op, amount, core.amount(9990) );
      FC_ASSERT( db.get_balance( alice_account,       core ).amount ==  999990 );
      top_up();
   }

   // Make sure we can withdraw the whole thing after 1000 seconds
   {
      const vesting_balance_object& vbo = create_vbo(
         alice_account.id, alice_account.id, core.amount( 10000 ), 1000, 1000);

      FC_ASSERT( db.get_balance( alice_account,       core ).amount ==  990000 );

      op.vesting_balance = vbo.id;
      op.owner = alice_account.id;
      // Withdraw one satoshi too much, no dice
      REQUIRE_THROW_WITH_VALUE( op, amount, core.amount(10001) );
      // Withdraw just the right amount, success!
      REQUIRE_OP_EVALUATION_SUCCESS( op, amount, core.amount(10000) );
      FC_ASSERT( db.get_balance( alice_account,       core ).amount == 1000000 );
   }

   // Make sure that we can't withdraw a single extra satoshi no matter how old it is
   {
      const vesting_balance_object& vbo = create_vbo(
         alice_account.id, alice_account.id, core.amount( 10000 ), 1000, 123456);

      FC_ASSERT( db.get_balance( alice_account,       core ).amount ==  990000 );

      op.vesting_balance = vbo.id;
      op.owner = alice_account.id;
      // Withdraw one satoshi too much, no dice
      REQUIRE_THROW_WITH_VALUE( op, amount, core.amount(10001) );
      // Withdraw just the right amount, success!
      REQUIRE_OP_EVALUATION_SUCCESS( op, amount, core.amount(10000) );
      FC_ASSERT( db.get_balance( alice_account,       core ).amount == 1000000 );
   }

   // Try withdrawing in three max installments:
   //   5000 after  500      seconds
   //   2000 after  400 more seconds
   //   3000 after 1000 more seconds
   {
      const vesting_balance_object& vbo = create_vbo(
         alice_account.id, alice_account.id, core.amount( 10000 ), 1000, 0);

      FC_ASSERT( db.get_balance( alice_account,       core ).amount ==  990000 );

      op.vesting_balance = vbo.id;
      op.owner = alice_account.id;
      REQUIRE_THROW_WITH_VALUE     ( op, amount, core.amount(   1) );
      spin_vbo_clock( vbo, 499 );
      REQUIRE_THROW_WITH_VALUE     ( op, amount, core.amount(5000) );
      spin_vbo_clock( vbo,   1 );
      REQUIRE_THROW_WITH_VALUE     ( op, amount, core.amount(5001) );
      REQUIRE_OP_EVALUATION_SUCCESS( op, amount, core.amount(5000) );
      FC_ASSERT( db.get_balance( alice_account,       core ).amount ==  995000 );

      spin_vbo_clock( vbo, 399 );
      REQUIRE_THROW_WITH_VALUE     ( op, amount, core.amount(2000) );
      spin_vbo_clock( vbo,   1 );
      REQUIRE_THROW_WITH_VALUE     ( op, amount, core.amount(2001) );
      REQUIRE_OP_EVALUATION_SUCCESS( op, amount, core.amount(2000) );
      FC_ASSERT( db.get_balance( alice_account,       core ).amount ==  997000 );

      spin_vbo_clock( vbo, 999 );
      REQUIRE_THROW_WITH_VALUE     ( op, amount, core.amount(3000) );
      spin_vbo_clock( vbo, 1   );
      REQUIRE_THROW_WITH_VALUE     ( op, amount, core.amount(3001) );
      REQUIRE_OP_EVALUATION_SUCCESS( op, amount, core.amount(3000) );
      FC_ASSERT( db.get_balance( alice_account,       core ).amount == 1000000 );
   }

   //
   // Increase by 10,000 csd / sec initially.
   // After 500 seconds, we have 5,000,000 csd.
   // Withdraw 2,000, we are now at 8,000 csd / sec.
   // At 8,000 csd / sec, it will take us 625 seconds to mature.
   //
   {
      const vesting_balance_object& vbo = create_vbo(
         alice_account.id, alice_account.id, core.amount( 10000 ), 1000, 0);

      FC_ASSERT( db.get_balance( alice_account,       core ).amount ==  990000 );

      op.vesting_balance = vbo.id;
      op.owner = alice_account.id;
      REQUIRE_THROW_WITH_VALUE     ( op, amount, core.amount(   1) );
      spin_vbo_clock( vbo, 500 );
      REQUIRE_OP_EVALUATION_SUCCESS( op, amount, core.amount(2000) );
      FC_ASSERT( db.get_balance( alice_account,       core ).amount ==  992000 );

      spin_vbo_clock( vbo, 624 );
      REQUIRE_THROW_WITH_VALUE     ( op, amount, core.amount(8000) );
      spin_vbo_clock( vbo,   1 );
      REQUIRE_THROW_WITH_VALUE     ( op, amount, core.amount(8001) );
      REQUIRE_OP_EVALUATION_SUCCESS( op, amount, core.amount(8000) );
      FC_ASSERT( db.get_balance( alice_account,       core ).amount == 1000000 );
   }
   // TODO:  Test with non-core asset and Bob account
} FC_LOG_AND_RETHROW() }

// TODO:  Write linear VBO tests

BOOST_AUTO_TEST_SUITE_END()
