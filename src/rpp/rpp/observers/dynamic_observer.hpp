//                   ReactivePlusPlus library
//
//           Copyright Aleksey Loginov 2023 - present.
//  Distributed under the Boost Software License, Version 1.0.
//     (See accompanying file LICENSE_1_0.txt or copy at
//           https://www.boost.org/LICENSE_1_0.txt)
//
//  Project home: https://github.com/victimsnino/ReactivePlusPlus

#pragma once

#include <rpp/disposables/fwd.hpp>
#include <rpp/observers/fwd.hpp>

#include <rpp/observers/observer.hpp>

#include <memory>
#include <utility>

namespace rpp::details::observers
{
    template<auto Fn, bool NoExcept>
    struct member_ptr_caller_impl;

    template<bool NoExcept, class T, class R, class... Args, R (T::*F)(Args...) noexcept(NoExcept)>
    struct member_ptr_caller_impl<F, NoExcept>
    {
        static R call(void* data, Args... args) noexcept(NoExcept) { return (static_cast<T*>(data)->*F)(static_cast<Args>(args)...); }
    };

    template<bool NoExcept, class T, class R, class... Args, R (T::*F)(Args...) const noexcept(NoExcept)>
    struct member_ptr_caller_impl<F, NoExcept>
    {
        static R call(const void* data, Args... args) noexcept(NoExcept) { return (static_cast<const T*>(data)->*F)(static_cast<Args>(args)...); }
    };

    template<auto Fn>
    using member_ptr_caller = member_ptr_caller_impl<Fn, noexcept(Fn)>;


    template<rpp::constraint::decayed_type Type>
    class dynamic_strategy final
    {
    public:
        template<rpp::constraint::observer_strategy<Type> Strategy>
            requires (!rpp::constraint::decayed_same_as<Strategy, dynamic_strategy<Type>>)
        explicit dynamic_strategy(observer<Type, Strategy>&& obs)
            : m_forwarder{std::make_shared<observer<Type, Strategy>>(std::move(obs))}
            , m_vtable{vtable::template create<observer<Type, Strategy>>()}
        {
        }

        dynamic_strategy(const dynamic_strategy&)     = default;
        dynamic_strategy(dynamic_strategy&&) noexcept = default;

        void set_upstream(const disposable_wrapper& d) noexcept { m_vtable->set_upstream(m_forwarder.get(), d); }

        bool is_disposed() const noexcept { return m_vtable->is_disposed(m_forwarder.get()); }

        void on_next(const Type& v) const noexcept { m_vtable->on_next_lvalue(m_forwarder.get(), v); }

        void on_next(Type&& v) const noexcept { m_vtable->on_next_rvalue(m_forwarder.get(), std::move(v)); }

        void on_error(const std::exception_ptr& err) const noexcept { m_vtable->on_error(m_forwarder.get(), err); }

        void on_completed() const noexcept { m_vtable->on_completed(m_forwarder.get()); }

    private:
        struct vtable
        {
            void (*on_next_lvalue)(const void*, const Type&){};
            void (*on_next_rvalue)(const void*, Type&&){};
            void (*on_error)(const void*, const std::exception_ptr&){};
            void (*on_completed)(const void*){};

            void (*set_upstream)(void*, const disposable_wrapper&){};
            bool (*is_disposed)(const void*){};

            template<rpp::constraint::observer Strategy>
            static const vtable* create() noexcept
            {
                static vtable s_res{
                    .on_next_lvalue = &member_ptr_caller<static_cast<typename Strategy::on_next_lvalue>(&Strategy::on_next)>::call,
                    .on_next_rvalue = &member_ptr_caller<static_cast<typename Strategy::on_next_rvalue>(&Strategy::on_next)>::call,
                    .on_error       = &member_ptr_caller<&Strategy::on_error>::call,
                    .on_completed   = &member_ptr_caller<&Strategy::on_completed>::call,
                    .set_upstream   = &member_ptr_caller<&Strategy::set_upstream>::call,
                    .is_disposed    = &member_ptr_caller<&Strategy::is_disposed>::call,
                };
                return &s_res;
            }
        };

    private:
        std::shared_ptr<void> m_forwarder;
        const vtable*         m_vtable;
    };
} // namespace rpp::details::observers


namespace rpp
{
    /**
     * @brief Type-erased version of the `rpp::observer`. Any observer can be converted to dynamic_observer via `rpp::observer::as_dynamic` member function.
     * @details To provide type-erasure it uses `std::shared_ptr`. As a result it has worse performance, but it is **ONLY** way to copy observer.
     *
     * @tparam Type of value this observer can handle
     *
     * @ingroup observers
     */
    template<constraint::decayed_type Type>
    class dynamic_observer final : public observer<Type, details::observers::dynamic_strategy<Type>>
    {
        using base = observer<Type, details::observers::dynamic_strategy<Type>>;

    public:
        using base::base;

        dynamic_observer(base&& b)
            : base{std::move(b)}
        {
        }

        dynamic_observer(const base& b)
            : base{b}
        {
        }
    };
} // namespace rpp