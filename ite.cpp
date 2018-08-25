#include <eosiolib/currency.hpp>
#include <math.h>
#include <eosiolib/crypto.h>
#include <eosiolib/eosio.hpp>
#include <eosiolib/transaction.hpp>
#include "Random.h"

#define ITE S(4, ITE)
#define SATOSHI S(4, SATOSHI)
#define GAME_SYMBOL S(4, LOG)
#define FEE_ACCOUNT N(itedecompany)
#define DEV_FEE_ACCOUNT N(itedeveloper)
#define TOKEN_CONTRACT N(eosio.token)

typedef double real_type;

using namespace eosio;
using namespace std;
using namespace eosblox;

class ite3 : public contract {
public:
    // parameters
    const uint64_t init_base_balance = 10000 * 10000ll;         // ITE token 总发行量 500万。
    const uint64_t init_quote_balance = 10 * 10000 * 10000ll; // 初始保证金 50 万 EOS。

    const uint64_t action_limit_time = 1;       // 操作冷却时间(s)
    const uint64_t max_operate_amount_ratio = 1; // 单笔可以买入的token数量
    const uint64_t game_start_time = 1535025600; // 项目启动时间 2018-08-23 20:00:00

    const uint64_t eco_fund_ratio = 5; // 每一笔投资，直接转入到 生态基金池 的比例

    const uint64_t pos_min_staked_time = 300;     // 最小 持币时长 需大于此时长 才能参与分红
    const uint64_t profit_sharing_period = 300;   // 每期分红的时间间隔: 7天
    const uint64_t profit_sharing_duration = 300; // 每期分红的分红持续时间: 7天

//    const uint64_t reset_price_limit_period = 6 * 3600; // 每6个小时，重新设置token价格的涨跌停线
//    const uint64_t token_price_increase_limit = 15;     // 最大涨幅，达到最大涨幅，暂时限制买入
//    const uint64_t token_price_decrease_limit = 15;     // 最大跌幅，达到最大跌幅，暂时限制卖出
    const uint64_t ROUND_SIZE = 10;     // 最大跌幅，达到最大跌幅，暂时限制卖出
    const uint64_t TOTAL_ROUNDS = 10000;     // 最大跌幅，达到最大跌幅，暂时限制卖出

    const uint64_t max_holding_lock = 1; // token总销售数 达到这个比例时。解锁个人持仓限制。
    const uint64_t first_sell_lock_ = 1; // token总出售数 达到这个比例之前。解锁token卖出限制。可以开始出售token。(未启用)

    ite3(account_name self)
            : contract(self),
              _global(_self, _self),
              _shares(_self, _self),
              _games(_self, _self),
              _market(_self, _self) {
        // Create a new global if not exists
        auto gl_itr = _global.begin();
        if (gl_itr == _global.end()) {
            gl_itr = _global.emplace(_self, [&](auto &gl) {
                gl.id = 0;
                gl.gameid = 1;
                gl.next_profit_sharing_time = now() + profit_sharing_period;
                gl.init_max = init_base_balance;
                gl.quote_balance.amount = init_quote_balance;
                gl.init_quote_balance.amount = init_quote_balance;
            });
        }

        // Create a new market if not exists
        auto market_itr = _market.begin();
        if (market_itr == _market.end()) {
            market_itr = _market.emplace(_self, [&](auto &m) {
                m.supply.amount = 100000000000000ll;
                m.supply.symbol = SATOSHI;
                m.base.balance.amount = init_base_balance;
                m.base.balance.symbol = ITE;
                m.quote.balance.amount = init_quote_balance;
                m.quote.balance.symbol = GAME_SYMBOL;
            });
        }

        // Create a new game if not exists
        auto game_itr = _games.find(gl_itr->gameid);
        if (game_itr == _games.end()) {
            game_itr = _games.emplace(_self, [&](auto &new_game) {
                new_game.gameid = gl_itr->gameid;
            });
        }
        // Create default referrer account if not exists
        user_resources_table default_ref_userres(_self, DEV_FEE_ACCOUNT);
        auto default_ref_res_itr = default_ref_userres.begin();

        if (default_ref_res_itr == default_ref_userres.end()) {
            default_ref_res_itr = default_ref_userres.emplace(_self, [&](auto &res) {
                res.referrer = DEV_FEE_ACCOUNT;
                res.owner = DEV_FEE_ACCOUNT;
                res.staked_time = now() + pos_min_staked_time;
            });
        }
    }

    void transfer(account_name from, account_name to, asset quantity, string memo) {
        if (from == _self || to != _self) {
            return;
        }

        eosio_assert(now() > game_start_time + random_offset(from),
                     "The ITE Token sell will start at 2018-08-23T20:00:00");

        eosio_assert(quantity.is_valid(), "Invalid token transfer");
        eosio_assert(quantity.amount > 0, "Quantity must be positive");

        // only accepts GAME_SYMBOL for buy
        if (quantity.symbol == GAME_SYMBOL) {
            bool isserial = false;
            auto quantity_remainder = quantity;
            auto quantity_single = quantity;
            auto quantity_int = quantity;
            quantity_single.amount = 10000;
            quantity_remainder.amount = quantity.amount % 10000;
            quantity_int.amount = quantity.amount / 10000;
            eosio_assert(quantity_remainder.amount == 0, "Quantity must be int");
            eosio_assert(quantity_int.amount > 0, "Quantity must be positive");
            eosio_assert(quantity_int.amount < 6, "Quantity must be less than 5");
            if (quantity_int.amount > 1) {
                isserial = true;
            }
            for (int i = 0; i < quantity_int.amount; i++) {
                buy(from, quantity_single, memo, isserial);
            }

        }
    }

    int64_t random_offset(account_name from) {
        checksum256 result;
        auto mixedBlock = tapos_block_prefix() * tapos_block_num() + from;

        const char *mixedChar = reinterpret_cast<const char *>(&mixedBlock);
        sha256((char *) mixedChar, sizeof(mixedChar), &result);
        const char *p64 = reinterpret_cast<const char *>(&result);
        auto x = 3, y = 50, z = 10;
        return (abs((int64_t) p64[x]) % (y)) + z;
    }


    void buy(account_name account, asset quant, string memo, bool isserial) {
        require_auth(account);
        print("a.");
        auto gl_itr = _global.begin();
        auto market_itr = _market.begin();
        if (gl_itr->gameid >= TOTAL_ROUNDS) {
            eosio_assert(1 < 0, "total_round is limited,just take a rest!");
        }
        print("0.");
        auto fee = quant;
        auto eco = quant;
        auto buy_sys = quant;
        auto game_reward = quant;

        fee.amount = (fee.amount + 195) / 50; /// .5% fee (round up)
        eco.amount = quant.amount * eco_fund_ratio / 100; /// .5% fee (round up)
        buy_sys.amount = quant.amount * eco_fund_ratio / 100; /// .5% fee (round up)
        game_reward.amount = game_reward.amount - fee.amount - eco.amount - buy_sys.amount;

        print("1.");
        //add
        auto game_itr = _games.find(gl_itr->gameid);

        _games.modify(game_itr, 0, [&](auto &game) {
            game.counter++;
            game.total_reward += game_reward;
        });
        user_games_info usergamestable(_self, account);

        auto res_game_itr = usergamestable.find(gl_itr->gameid);
        //reserve 10 games data

        // add new player into players table
        player_index _players(_self, gl_itr->gameid);

        _players.emplace(_self, [&](auto &new_ticket) {
            new_ticket.player_name = account;
            new_ticket.lucky_num = game_itr->counter;
            new_ticket.join_time = current_time();
        });
        if (res_game_itr == usergamestable.end()) {
            res_game_itr = usergamestable.emplace(account, [&](auto &res) {
                res.gameid = gl_itr->gameid;
                res.owner = account;
                res.action_count++;
                res.in += quant;
            });
        } else {
            // time limit
            auto time_diff = (current_time() - res_game_itr->last_action_time) / 1000 / 1000;
            eosio_assert(time_diff > action_limit_time || isserial, "please wait a moment");

            usergamestable.modify(res_game_itr, account, [&](auto &res) {
                res.last_action_time = current_time();
                res.action_count++;
                res.in += quant;
            });
        }

        print("2.");
        if (fee.amount > 0) {
            action(
                    permission_level{_self, N(active)},
                    TOKEN_CONTRACT, N(transfer),
                    make_tuple(_self, FEE_ACCOUNT, fee, string("buy fee"))
            ).send();
            std::string msg =
                    "you get lucky_num:" + to_string(game_itr->counter) + ",in gameid:" + to_string(gl_itr->gameid);
            action(
                    permission_level{_self, N(active)},
                    TOKEN_CONTRACT, N(transfer),
                    make_tuple(_self, account, asset(1, GAME_SYMBOL), string(msg))
            ).send();

        }
        print("3.");
        int64_t ite_out;

        _market.modify(market_itr, 0, [&](auto &es) {
            ite_out = es.convert(buy_sys, ITE).amount;
        });
        print("ite_out:", ite_out);
        eosio_assert(ite_out > 0, "must reserve a positive amount");

        // max operate amount limit
        auto max_operate_amount = gl_itr->init_max / 100 * max_operate_amount_ratio;
        eosio_assert(ite_out < max_operate_amount, "must reserve less than max operate amount:");

        _global.modify(gl_itr, 0, [&](auto &gl) {
            gl.counter++;
            gl.total_reserved += ite_out;
            gl.quote_balance += buy_sys;
            gl.eco_fund_balance += eco;
        });

        auto share_ite_out = ite_out;
        auto ite_out_after_share = ite_out;
        share_ite_out = ite_out / 10;
        ite_out_after_share -= share_ite_out;
        user_resources_table userres(_self, account);
        auto res_itr = userres.begin();

        if (res_itr == userres.end()) {
            //get referrer
            account_name referrer = string_to_name(memo.c_str());

            user_resources_table ref_userres(_self, referrer);

            auto ref_res_itr = ref_userres.begin();

            if (ref_res_itr == ref_userres.end()) {
                referrer = DEV_FEE_ACCOUNT;
            }


            res_itr = userres.emplace(account, [&](auto &res) {
                res.referrer = referrer;
                res.owner = account;
                res.hodl = ite_out_after_share;
                res.action_count++;
                res.fee_amount += fee;
                res.out += quant;
                res.staked_time = now() + pos_min_staked_time;
            });

            if (referrer == DEV_FEE_ACCOUNT) {
                user_resources_table dev_ref_userres(_self, referrer);
                auto dev_ref_res_itr = dev_ref_userres.begin();

                dev_ref_userres.modify(dev_ref_res_itr, account, [&](auto &res) {
                    res.hodl += share_ite_out;
                    res.total_share_ite += share_ite_out;
                    res.ref_count++;
                });
            } else {
                ref_userres.modify(ref_res_itr, account, [&](auto &res) {
                    res.hodl += share_ite_out;
                    res.total_share_ite += share_ite_out;
                    res.ref_count++;
                });
            }
        } else {
            // time limit
            auto time_diff = now() - res_itr->last_action_time;
            eosio_assert(time_diff > action_limit_time || isserial, "please wait a moment");

            // max hold limit
            auto max_holding_lock_line = gl_itr->init_max / 100 * max_holding_lock;
            if (gl_itr->total_reserved < max_holding_lock_line) {
                eosio_assert((res_itr->hodl + ite_out) <= max_operate_amount, "can not hold more than 1% before 15%");
            }

            userres.modify(res_itr, account, [&](auto &res) {
                res.hodl += ite_out;
                res.last_action_time = now();
                res.staked_time = now() + pos_min_staked_time;
                res.action_count++;
                res.fee_amount += fee;
                res.out += quant;
            });

            if (share_ite_out > 0) {
                user_resources_table ref_userres(_self, res_itr->referrer);

                auto ref_res_itr = ref_userres.begin();

                ref_userres.modify(ref_res_itr, account, [&](auto &res) {
                    res.hodl += share_ite_out;
                    res.total_share_ite += share_ite_out;
                });
            }
        }

        trigger_profit_sharing();
        trigger_game_over(account, quant);
    }

    void trigger_game_over(account_name account, asset quant) {
        auto gl_itr = _global.begin();
        auto game_itr = _games.find(gl_itr->gameid);

        bool gameover = false;

        if (game_itr->counter >= ROUND_SIZE) {
            gameover = true;
        }

        if (gameover) {
            // reward = {end_prize_times} * quant , but, <= {end_prize_ratio}% * quote_balance <= max_end_prize
            auto reward = quant;
            auto lucky_num = randomblox(ROUND_SIZE);
            player_index _players(_self, gl_itr->gameid);
            auto player_itr = _players.find(lucky_num);
            auto lucky_man = player_itr->player_name;
            reward.amount = game_itr->total_reward.amount;

            eosio_assert(reward.amount > 0, "shit happens again");

            // transfer to lucky
            action(
                    permission_level{_self, N(active)},
                    TOKEN_CONTRACT, N(transfer),
                    make_tuple(_self, lucky_man, reward, string("lucky reward!"))
            ).send();

            // change game status
            _games.modify(game_itr, 0, [&](auto &game) {
                game.status = 1;
                game.total_reward -= reward;
                game.lucky_man = lucky_man;
                game.lucky_reward = reward;
                game.end_time = current_time();
                game.lucky_id = lucky_num;
            });

            // increment global game counter
            _global.modify(gl_itr, 0, [&](auto &gl) {
                gl.gameid++;
            });

            // create new game
            _games.emplace(_self, [&](auto &new_game) {
                new_game.gameid = gl_itr->gameid;
                new_game.counter = 0;
                new_game.start_time = current_time();
            });
        }
    }
    /// @abi action
    void sell(account_name account, int64_t amount) {
        require_auth(account);
        eosio_assert(amount > 0, "cannot sell negative amount");

        auto gl_itr = _global.begin();

        user_resources_table userres(_self, account);
        auto res_itr = userres.begin();

        eosio_assert(res_itr != userres.end(), "no resource row");
        eosio_assert(res_itr->hodl >= amount, "insufficient quota");

        // time limit
        auto time_diff = now() - res_itr->last_action_time;
        eosio_assert(time_diff > action_limit_time, "please wait a moment");

        // max operate amount limit
        auto max_operate_amount = gl_itr->init_max / 100 * max_operate_amount_ratio;
        eosio_assert(amount < max_operate_amount, "must sell less than max operate amount");

        asset tokens_out;

        auto itr = _market.begin();

        _market.modify(itr, 0, [&](auto &es) {
            tokens_out = es.convert(asset(amount, ITE), GAME_SYMBOL);
        });

        eosio_assert(tokens_out.amount > 0, "must payout a positive amount");

        auto max = gl_itr->quote_balance - gl_itr->init_quote_balance;

        if (tokens_out > max) {
            tokens_out = max;
        }

        _global.modify(gl_itr, 0, [&](auto &gl) {
            gl.counter++;
            gl.total_reserved -= amount;
            gl.quote_balance -= tokens_out;
        });

        auto fee = (tokens_out.amount + 199) / 200; /// .5% fee (round up)
        auto action_total_fee = fee;

        auto quant_after_fee = tokens_out;
        quant_after_fee.amount -= fee;

        userres.modify(res_itr, account, [&](auto &res) {
            res.hodl -= amount;
            res.last_action_time = now();
            res.action_count++;
            res.fee_amount += asset(action_total_fee, GAME_SYMBOL);
            res.in += tokens_out;
        });

        action(
                permission_level{_self, N(active)},
                TOKEN_CONTRACT, N(transfer),
                make_tuple(_self, account, quant_after_fee, string("sell payout")))
                .send();

        if (fee > 0) {
            action(
                    permission_level{_self, N(active)},
                    TOKEN_CONTRACT, N(transfer),
                    make_tuple(_self, FEE_ACCOUNT, asset(fee, GAME_SYMBOL), string("sell fee")))
                    .send();
        }

        trigger_profit_sharing();
    }

    /**
    * 触发系统保护开关、状态切换。
    * 涨跌停开关
    * 熔断开关
    */
    void trigger_system_protection() {
        // TODO
    }

    /*
    * 分红，是拿出生态基金池 中的EOS，对token持有者进行分红。持有越多的token, 将获得越多的EOS分红。
    * 这是一种类似 POS 的分配方式，所以，我们定义了一个 ”持有时间“ 的概念。 当账户满足 最小 ”持有时间“的时候，才能参与分红。
    * 在持有时间内，如果进行任意数量的买入操作，将重新计算 ”持有时间“。 这是为了防止有人在 分红期间，大量买入，拿到分红以后，立刻大量卖出。
    *
    * 为了将去中心化进行到底。我们设计了 “投票发起分红” 的机制。
    * 在系统启动的七天后，系统将自动启动投票， 当投票总数，超过当前售出token总量的15%。
    * 则进入为期三天的分红周期。三天后，本期分红结束，系统进入7天的分红冷却时间
    * 当期分红结束以后，未分红奖励，进入下一轮
    */
    void trigger_profit_sharing_vote() {
        // TODO
    }

    /**
    * 由系统定时自动发起分红的方案
    */
    void trigger_profit_sharing() {
        auto gl_itr = _global.begin();

        // 检测是否到了分红时间
        if (now() > gl_itr->next_profit_sharing_time) {
            auto max_share = gl_itr->eco_fund_balance;

            // 每周分红一次，每次只分红基金池中的10分之一。保证可持续性分红。
            max_share.amount = max_share.amount * 20 / 100;

            auto eos_per_ite = max_share;

            if (gl_itr->total_reserved > 0) {
                eos_per_ite.amount = max_share.amount / gl_itr->total_reserved;
            }

            // 只有 eos_per_ite > 0 才发起分红
            if (eos_per_ite.amount > 0) {
                // create a new profit sharing record
                _shares.emplace(_self, [&](auto &ps) {
                    ps.id = _shares.available_primary_key();
                    ps.eco_fund_balance_snapshoot = gl_itr->eco_fund_balance;
                    ps.quote_balance_snapshoot = gl_itr->quote_balance;
                    ps.reserved_snapshoot = gl_itr->total_reserved;
                    ps.total_share_balance = max_share;
                    ps.eos_per_ite = eos_per_ite;
                    ps.start_time = now();
                    ps.end_time = now() + profit_sharing_duration;
                });

                // 设置下一次分红时间
                _global.modify(gl_itr, 0, [&](auto &gl) {
                    gl.next_profit_sharing_time = now() + profit_sharing_period;
                });
            }
        }
    }
    /// @abi action
    void claim(account_name account, int64_t shareid) {
        require_auth(account);

        auto ps_itr = _shares.find(shareid);

        eosio_assert(ps_itr != _shares.end(), "sorry,profit share can't be found!");
//        eosio_assert(now() < ps_itr->end_time, "the profit share has expired");

        user_resources_table userres(_self, account);
        auto res_itr = userres.begin();

        eosio_assert(res_itr != userres.end(), "you are not an game user,play quickly!");

        claims_index _user_claims(_self, account);
        auto claim_itr = _user_claims.find(ps_itr->id);
        eosio_assert(claim_itr == _user_claims.end(), "you can't claim this shareid,maybe you had claimed it!");

        eosio_assert(now() > res_itr->staked_time, "you can't get this profit share now because of token staked time");
        eosio_assert(now() > res_itr->next_claim_time, "in claim time cooldown");

        auto reward = ps_itr->eos_per_ite;
        reward.amount = reward.amount * res_itr->hodl;

        userres.modify(res_itr, account, [&](auto &res) {
            res.claim_count++;
            res.claim += reward;
            res.last_claim_time = now();
            res.next_claim_time = now() + profit_sharing_duration;
        });

        // create new user claim record
        _user_claims.emplace(_self, [&](auto &new_claim) {
            new_claim.share_id = ps_itr->id;
            new_claim.my_ite_snapshoot = res_itr->hodl;
            new_claim.reward = reward;
        });

        if (reward.amount > 0) {
            action(
                    permission_level{_self, N(active)},
                    TOKEN_CONTRACT, N(transfer),
                    make_tuple(_self, account, reward, string("claim profit sharing reward")))
                    .send();

            auto gl_itr = _global.begin();
            _global.modify(gl_itr, 0, [&](auto &gl) {
                gl.eco_fund_balance -= reward;
            });
        }
    }


    /// @abi action
    void erasedata() {
        require_auth(_self);
        auto game_itr = _games.begin();
        if(game_itr == _games.end()){
            return;
        }
        _games.erase(game_itr);
        player_index _players(_self, game_itr->gameid);
        for (int i = 1; i <= ROUND_SIZE; i++) {
            auto player_itr = _players.find(i);
            account_name player_name = player_itr->player_name;
            user_games_info userres(_self, player_name);
            auto res_itr = userres.find(game_itr->gameid);
            if(res_itr != userres.end()) {
                userres.erase(res_itr);
            }
            _players.erase(player_itr);
        };

    }

private:
    // @abi table global i64
    struct global {
        uint64_t id = 0;
        uint64_t gameid = 1;

        uint64_t counter;
        uint64_t init_max;
        uint64_t total_reserved;
        uint64_t start_time = now();
        uint64_t next_profit_sharing_time;

        asset quote_balance = asset(0, GAME_SYMBOL);
        asset init_quote_balance = asset(0, GAME_SYMBOL);
        asset eco_fund_balance = asset(0, GAME_SYMBOL);

        uint64_t primary_key() const { return id; }

        EOSLIB_SERIALIZE(global, (id)(gameid)(counter)(init_max)(total_reserved)(start_time)(next_profit_sharing_time)(
                quote_balance)(init_quote_balance)(eco_fund_balance)
        )
    };

    typedef eosio::multi_index<N(global), global> global_index;
    global_index _global;

    // @abi table shares i64
    struct shares {
        uint64_t id;
        asset eco_fund_balance_snapshoot = asset(0, GAME_SYMBOL); // 生态基金池快照
        asset quote_balance_snapshoot = asset(0, GAME_SYMBOL);    // 市值快照
        uint64_t reserved_snapshoot;                              // ITE Token 售出快照
        asset total_share_balance = asset(0, GAME_SYMBOL);        // 本期的分红总额
        asset eos_per_ite = asset(0, GAME_SYMBOL);                // 每个ITE可以分得多少EOS
        uint64_t start_time;                                      // 本期分红开始时间
        uint64_t end_time;                                        // 本期分红截止时间（每期持续n天）

        uint64_t primary_key() const { return id; }

        EOSLIB_SERIALIZE(shares, (id)(eco_fund_balance_snapshoot)(quote_balance_snapshoot)(reserved_snapshoot)(
                total_share_balance)(eos_per_ite)(start_time)(end_time)
        )
    };

    typedef eosio::multi_index<N(shares), shares> shares_index;
    shares_index _shares;

    // @abi table claims i64
    struct claims {
        uint64_t share_id;
        uint64_t my_ite_snapshoot;
        uint64_t claim_date = now();
        asset reward = asset(0, GAME_SYMBOL);

        uint64_t primary_key() const { return share_id; }

        EOSLIB_SERIALIZE(claims, (share_id)(my_ite_snapshoot)(claim_date)(reward)
        )
    };

    typedef eosio::multi_index<N(claims), claims> claims_index;


    // @abi table player i64
    struct player {
        uint32_t lucky_num;
        account_name player_name;
        int64_t join_time = current_time();

        uint64_t primary_key() const { return lucky_num; }

        EOSLIB_SERIALIZE(player, (lucky_num)(player_name)(join_time)
        )
    };

    typedef eosio::multi_index<N(player), player> player_index;

    // @abi table userinfo i64
    struct userinfo {
        account_name owner;
        account_name referrer;                    // 推荐人
        int64_t hodl;                             // 持有智子数量
        int64_t total_share_ite;                  // 累计推荐奖励ITE
        int64_t ref_count;                        // 累计推荐人
        int64_t claim_count;                      // 参与领分红次数
        int64_t action_count;                     // 累计操作次数
        int64_t last_action_time = now();         // 上一次操作时间
        int64_t last_claim_time;                  // 上一次领分红时间
        int64_t next_claim_time;                  // 下一次可领分红时间
        int64_t staked_time;                      // 锁币时间
        asset fee_amount = asset(0, GAME_SYMBOL); // 累计手续费
        asset in = asset(0, GAME_SYMBOL);         // 累计收入
        asset out = asset(0, GAME_SYMBOL);        // 累计支出
        asset claim = asset(0, GAME_SYMBOL);      // 累计分红
        int64_t join_time = now();

        uint64_t primary_key() const { return owner; }

        EOSLIB_SERIALIZE(userinfo, (owner)(referrer)(hodl)(total_share_ite)(ref_count)(claim_count)(action_count)(
                last_action_time)(last_claim_time)(next_claim_time)(staked_time)(fee_amount)(in)(out)(claim)(join_time)
        )
    };

    typedef eosio::multi_index<N(userinfo), userinfo> user_resources_table;

    // @abi table usergames i64
    struct usergames {
        uint64_t gameid;
        account_name owner;
        int64_t action_count;
        int64_t last_action_time = current_time();
        asset in = asset(0, GAME_SYMBOL);

        uint64_t primary_key() const { return gameid; }

        EOSLIB_SERIALIZE(usergames, (gameid)(owner)(action_count)(last_action_time)(in)
        )
    };

    typedef eosio::multi_index<N(usergames), usergames> user_games_info;


    // @abi table game i64
    struct game {
        uint64_t gameid;
        uint64_t status;
        uint64_t counter;
        uint64_t start_time = current_time();
        uint64_t end_time;
        asset total_reward = asset(0, GAME_SYMBOL);
        asset lucky_reward = asset(0, GAME_SYMBOL);
        account_name lucky_man;
        uint64_t lucky_id;

        uint64_t primary_key() const { return gameid; }

        EOSLIB_SERIALIZE(game, (gameid)(status)(counter)(start_time)(end_time)(total_reward)(lucky_reward)(
                lucky_man)(
                lucky_id)
        )
    };

    typedef eosio::multi_index<N(game), game> game_index;
    game_index _games;

    /**
      *  Uses Bancor math to create a 50/50 relay between two asset types. The state of the
      *  bancor exchange is entirely contained within this struct. There are no external
      *  side effects associated with using this API.
      *  Love BM. Love Bancor.
      */
    struct exchange_state {
        uint64_t id = 0;

        asset supply;

        struct connector {
            asset balance;
            double weight = .5;

            EOSLIB_SERIALIZE(connector, (balance)(weight))
        };

        connector base;
        connector quote;

        uint64_t primary_key() const { return id; }

        asset convert_to_exchange(connector &c, asset in) {
            real_type R(supply.amount);
            real_type C(c.balance.amount + in.amount);
            real_type F(c.weight / 1000.0);
            real_type T(in.amount);
            real_type ONE(1.0);

            real_type E = -R * (ONE - pow(ONE + T / C, F));
            int64_t issued = int64_t(E);

            supply.amount += issued;
            c.balance.amount += in.amount;

            return asset(issued, supply.symbol);
        }

        asset convert_from_exchange(connector &c, asset in) {
            eosio_assert(in.symbol == supply.symbol, "unexpected asset symbol input");

            real_type R(supply.amount - in.amount);
            real_type C(c.balance.amount);
            real_type F(1000.0 / c.weight);
            real_type E(in.amount);
            real_type ONE(1.0);

            real_type T = C * (pow(ONE + E / R, F) - ONE);
            int64_t out = int64_t(T);

            supply.amount -= in.amount;
            c.balance.amount -= out;

            return asset(out, c.balance.symbol);
        }

        asset convert(asset from, symbol_type to) {
            auto sell_symbol = from.symbol;
            auto ex_symbol = supply.symbol;
            auto base_symbol = base.balance.symbol;
            auto quote_symbol = quote.balance.symbol;

            if (sell_symbol != ex_symbol) {
                if (sell_symbol == base_symbol) {
                    from = convert_to_exchange(base, from);
                } else if (sell_symbol == quote_symbol) {
                    from = convert_to_exchange(quote, from);
                } else {
                    eosio_assert(false, "invalid sell");
                }
            } else {
                if (to == base_symbol) {
                    from = convert_from_exchange(base, from);
                } else if (to == quote_symbol) {
                    from = convert_from_exchange(quote, from);
                } else {
                    eosio_assert(false, "invalid conversion");
                }
            }

            if (to != from.symbol)
                return convert(from, to);

            return from;
        }

        EOSLIB_SERIALIZE(exchange_state, (supply)(base)(quote)
        )
    };

    typedef eosio::multi_index<N(market), exchange_state> market;
    market _market;


    int64_t randomblox(int64_t range) {
        checksum256 result;
        auto mixedBlock = tapos_block_prefix() + tapos_block_num();
        const char *mixedChar = reinterpret_cast<const char *>(&mixedBlock);
        sha256((char *) mixedChar, sizeof(mixedChar), &result);
        const char *p64 = reinterpret_cast<const char *>(&result);
        auto seed = (abs((int64_t) p64[0]) % (1024 + 1 - 1)) + 1;
        Random gen(seed);
        auto random_result = gen.next();
        auto random_range = random_result % (range + 1 - 1) + 1;
        return random_range;
    }
};

#define EOSIO_ABI_PRO(TYPE, MEMBERS)                                                                                      \
  extern "C" {                                                                                                            \
  void apply(uint64_t receiver, uint64_t code, uint64_t action)                                                           \
  {                                                                                                                       \
    auto self = receiver;                                                                                                 \
    if (action == N(onerror))                                                                                             \
    {                                                                                                                     \
      eosio_assert(code == N(eosio), "onerror action's are only valid from the \"eosio\" system account");                \
    }                                                                                                                     \
    if ((code == TOKEN_CONTRACT && action == N(transfer)) || (code == self && (action == N(sell) || action == N(claim)|| action == N(erasedata)))) \
    {                                                                                                                     \
      TYPE thiscontract(self);                                                                                            \
      switch (action)                                                                                                     \
      {                                                                                                                   \
        EOSIO_API(TYPE, MEMBERS)                                                                                          \
      }                                                                                                                   \
    }                                                                                                                     \
  }                                                                                                                       \
  }

EOSIO_ABI_PRO(ite3, (transfer)(sell)(claim)(erasedata))
