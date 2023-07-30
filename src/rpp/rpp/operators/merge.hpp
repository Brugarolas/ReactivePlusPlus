//                  ReactivePlusPlus library
//
//          Copyright Aleksey Loginov 2023 - present.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          https://www.boost.org/LICENSE_1_0.txt)
//
// Project home: https://github.com/victimsnino/ReactivePlusPlus
//

#pragma once

#include <rpp/operators/fwd.hpp>
#include <rpp/schedulers/current_thread.hpp>
#include <rpp/disposables/composite_disposable.hpp>

#include <rpp/defs.hpp>
#include <rpp/operators/details/strategy.hpp>
#include <atomic>
#include <cstddef>
#include <mutex>
#include <rpp/utils/tuple.hpp>

namespace rpp::operators::details
{
template<typename Lock>
class merge_disposable final : public composite_disposable
{
public:
    std::lock_guard<Lock> lock_guard() { return std::lock_guard<Lock>{m_lock}; }

    void increment_on_completed() { m_on_completed_needed.fetch_add(1, std::memory_order_relaxed); }
    bool decrement_on_completed() { return m_on_completed_needed.fetch_sub(1, std::memory_order::acq_rel) == 1; }

private:
    Lock               m_lock{};
    std::atomic_size_t m_on_completed_needed{};
};

struct merge_observer_inner_strategy
{
    std::shared_ptr<merge_disposable<std::mutex>> disposable;

    static constexpr empty_on_subscribe on_subscribe{};

    void set_upstream(const rpp::constraint::observer auto&, const rpp::disposable_wrapper& d) const
    {
        disposable->add(d.get_original());
    }

    bool is_disposed(const rpp::constraint::observer auto& obs) const
    {
        return disposable->is_disposed() || obs.is_disposed();
    }

    template<typename T>
    void on_next(const rpp::constraint::observer auto& obs, T&& v) const
    {
        auto lock = disposable->lock_guard();
        obs.on_next(std::forward<T>(v));
    }

    void on_error(const rpp::constraint::observer auto & obs, const std::exception_ptr& err) const
    {
        disposable->dispose();

        auto lock = disposable->lock_guard();
        obs.on_error(err);
    }

    void on_completed(const rpp::constraint::observer auto& obs) const
    {
        if (disposable->decrement_on_completed())
        {
            disposable->dispose();

            auto lock = disposable->lock_guard();
            obs.on_completed();
        }
    }
};

template<rpp::constraint::decayed_type Value>
struct merge_observer_strategy
{
    std::shared_ptr<merge_disposable<std::mutex>> disposable = std::make_shared<merge_disposable<std::mutex>>();

    void on_subscribe(rpp::constraint::observer auto& obs) const
    {
        disposable->increment_on_completed();
        obs.set_upstream(disposable_wrapper::from_weak(disposable));
    }

    void set_upstream(const rpp::constraint::observer auto&, const rpp::disposable_wrapper& d) const
    {
        disposable->add(d.get_original());
    }

    bool is_disposed(const rpp::constraint::observer auto& obs) const
    {
        return disposable->is_disposed() || obs.is_disposed();
    }

    template<rpp::constraint::observer TObs, typename T>
    void on_next(TObs&& obs, T&& v) const
    {
        disposable->increment_on_completed();
        std::forward<T>(v).subscribe(rpp::observer<Value, operator_strategy_base<Value, std::decay_t<TObs>, merge_observer_inner_strategy>>{std::forward<TObs>(obs), disposable});
    }

    void on_error(const rpp::constraint::observer auto & obs, const std::exception_ptr& err) const
    {
        disposable->dispose();

        auto lock = disposable->lock_guard();
        obs.on_error(err);
    }

    void on_completed(const rpp::constraint::observer auto& obs) const
    {
        if (disposable->decrement_on_completed())
        {
            disposable->dispose();

            auto lock = disposable->lock_guard();
            obs.on_completed();
        }
    }
};

struct merge_t
{
    template<rpp::constraint::decayed_type T>
        requires rpp::constraint::observable<T>
    using ResultValue = rpp::utils::extract_observable_type_t<T>;

    template<rpp::constraint::observer Observer, typename... Strategies>
    void subscribe(Observer&& observer, const observable_chain_strategy<Strategies...>& strategy) const
    {
        // Need to take ownership over current_thread in case of inner-observables also uses them
        auto drain_on_exit = rpp::schedulers::current_thread::own_queue_and_drain_finally_if_not_owned();

        using InnerObservable = typename observable_chain_strategy<Strategies...>::ValueType;
        using Value = rpp::utils::extract_observable_type_t<InnerObservable>;

        strategy.subscribe(rpp::observer<InnerObservable, operator_strategy_base<InnerObservable, rpp::dynamic_observer<Value>, merge_observer_strategy<Value>>>{std::forward<Observer>(observer).as_dynamic()});
    }

};

template<rpp::constraint::observable... TObservables>
struct merge_with_t
{
    RPP_NO_UNIQUE_ADDRESS rpp::utils::tuple<TObservables...> observables{};

    template<rpp::constraint::decayed_type T>
        requires (std::same_as<T, utils::extract_observable_type_t<TObservables>> && ...)
    using ResultValue = T;

    template<rpp::constraint::observer Observer, typename... Strategies>
    void subscribe(Observer&& observer, const observable_chain_strategy<Strategies...>& observable_strategy) const
    {
        using Value = typename observable_chain_strategy<Strategies...>::ValueType;

        auto obs_as_dynamic = std::forward<Observer>(observer).as_dynamic();

        merge_observer_strategy<Value> strategy{};

        // Need to take ownership over current_thread in case of inner-observables also uses them
        auto drain_on_exit = rpp::schedulers::current_thread::own_queue_and_drain_finally_if_not_owned();

        strategy.on_subscribe(obs_as_dynamic);
        strategy.on_next(obs_as_dynamic, observable_strategy);
        observables.apply(&apply<decltype(obs_as_dynamic), Value>, strategy, obs_as_dynamic);
        strategy.on_completed(obs_as_dynamic);
    }

private:
    template<rpp::constraint::observer Observer, typename Value>
    static void apply(const merge_observer_strategy<Value>& strategy, const Observer& obs_as_dynamic, const TObservables& ...observables)
    {
        (strategy.on_next(obs_as_dynamic, observables), ...);
    }
};
}

namespace rpp::operators
{
/**
 * @brief Converts observable of observables of items into observable of items via merging emissions.
 *
 * @warning According to observable contract (https://reactivex.io/documentation/contract.html) emissions from any observable should be serialized, so, resulting observable uses mutex to satisfy this requirement
 *
 * @warning During on subscribe operator takes ownership over rpp::schedulers::current_thread to allow mixing of underlying emissions
 *
 * @marble merge
     {
         source observable                :
         {
             +--1-2-3-|
             .....+4--6-|
         }
         operator "merge" : +--1-243-6-|
     }
 *
 * @details Actually it subscribes on each observable from emissions. Resulting observables completes when ALL observables completes
 *
 * @par Performance notes:
 * - 2 heap allocation (1 for state, 1 to convert observer to dynamic_observer)
 * - Acquiring mutex during all observer's calls
 *
 * @warning #include <rpp/operators/merge.hpp>
 *
 * @par Example:
 * @snippet merge.cpp merge
 *
 * @ingroup combining_operators
 * @see https://reactivex.io/documentation/operators/merge.html
 */
inline auto merge()
{
    return details::merge_t{};
}

/**
 * @brief Combines submissions from current observable with other observables into one
 *
 * @warning According to observable contract (https://reactivex.io/documentation/contract.html) emissions from any observable should be serialized, so, resulting observable uses mutex to satisfy this requirement
 *
 * @warning During on subscribe operator takes ownership over rpp::schedulers::current_thread to allow mixing of underlying emissions
 *
 * @marble merge_with
     {
         source original_observable: +--1-2-3-|
         source second: +-----4--6-|
         operator "merge_with" : +--1-243-6-|
     }
 *
 * @details Actually it subscribes on each observable. Resulting observables completes when ALL observables completes
 *
 * @par Performance notes:
 * - 2 heap allocation (1 for state, 1 to convert observer to dynamic_observer)
 * - Acquiring mutex during all observer's calls
 *
 * @param observables are observables whose emissions would be merged with current observable
 * @warning #include <rpp/operators/merge.hpp>
 *
 * @par Example:
 * @snippet merge.cpp merge_with
 *
 * @ingroup combining_operators
 * @see https://reactivex.io/documentation/operators/merge.html
 */
template<rpp::constraint::observable TObservable, rpp::constraint::observable... TObservables>
    requires constraint::observables_of_same_type<std::decay_t<TObservable>, std::decay_t<TObservables>...>
auto merge_with(TObservable&& observable, TObservables&& ...observables)
{
    return details::merge_with_t<std::decay_t<TObservable>, std::decay_t<TObservables>...>{rpp::utils::tuple{std::forward<TObservable>(observable), std::forward<TObservables>(observables)...}};
}
} // namespace rpp::operators
