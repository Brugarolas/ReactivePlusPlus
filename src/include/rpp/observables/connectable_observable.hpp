//                   ReactivePlusPlus library
// 
//           Copyright Aleksey Loginov 2022 - present.
//  Distributed under the Boost Software License, Version 1.0.
//     (See accompanying file LICENSE_1_0.txt or copy at
//           https://www.boost.org/LICENSE_1_0.txt)
// 
//  Project home: https://github.com/victimsnino/ReactivePlusPlus

#pragma once

#include <rpp/observables/constraints.hpp>
#include <rpp/subscribers/constraints.hpp>
#include <rpp/subjects/constraints.hpp>
#include <rpp/subjects/type_traits.hpp>
#include <rpp/utils/utilities.hpp>
#include <rpp/operators/fwd/ref_count.hpp>

namespace rpp
{
template<constraint::decayed_type                    Type,
         subjects::constraint::subject_of_type<Type> Subject,
         constraint::observable_of_type<Type>        OriginalObservable>
class connectable_observable
    : public decltype(std::declval<Subject>().get_observable())
    , public details::member_overload<Type, connectable_observable<Type, Subject, OriginalObservable>, details::ref_count_tag>
{
    using base = decltype(std::declval<Subject>().get_observable());
public:
    connectable_observable(const OriginalObservable& original_observable, const Subject& subject = Subject{})
        : base{subject.get_observable()}
        , m_original_observable{original_observable}
        , m_subject{subject} {}

    connectable_observable(OriginalObservable&& original_observable, const Subject& subject = Subject{})
        : base{subject.get_observable()}
        , m_original_observable{std::move(original_observable)}
        , m_subject{subject} {}

    composite_subscription connect(const composite_subscription& subscription = composite_subscription{}) const
    {
        auto subscriber              = m_subject.get_subscriber();
        auto subscriber_subscription = subscriber.get_subscription();

        {
            std::lock_guard lock(m_state->mutex);

            if (!m_state->sub.is_empty())
                return subscription;

            m_state->sub = subscriber_subscription.add(subscription);
            m_original_observable.subscribe(m_state->sub, subscriber.get_observer());
        }

        subscription.add([state = m_state, subscriber_subscription]
        {
            auto current_sub = composite_subscription::empty();
            {
                std::lock_guard lock(state->mutex);
                std::swap(current_sub, state->sub);
            }
            current_sub.unsubscribe();
            subscriber_subscription.remove(current_sub);
        });


        return subscription;
    }

private:
    OriginalObservable m_original_observable;
    Subject            m_subject;

    struct state_t
    {
        std::mutex             mutex{};
        composite_subscription sub = composite_subscription::empty();
    };

    std::shared_ptr<state_t> m_state = std::make_shared<state_t>();
};

template<constraint::observable OriginalObservable, subjects::constraint::subject Subject>
connectable_observable(const OriginalObservable&, const Subject&) -> connectable_observable<subjects::utils::extract_subject_type_t<Subject>, Subject, OriginalObservable>;
} // namespace rpp
