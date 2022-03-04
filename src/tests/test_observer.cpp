// MIT License
// 
// Copyright (c) 2022 Aleksey Loginov
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include <catch2/catch_test_macros.hpp>

#include <rpp/observer.h>

SCENARIO("on_next, on_error and on_completed can be obtained")
{
    size_t on_next_count{}, on_error_count{}, on_completed_count{};

    auto validate_observer = [&](const auto& observer)
    {
        WHEN("Call on_next")
        {
            observer.on_next(1);
            THEN("on_next obtained")
            {
                REQUIRE(on_next_count == 1);
                REQUIRE(on_error_count == 0);
                REQUIRE(on_completed_count == 0);
            }
        }
        WHEN("Call on_error")
        {
            observer.on_error(std::make_exception_ptr(std::exception{}));
            THEN("on_next obtained")
            {
                REQUIRE(on_next_count == 0);
                REQUIRE(on_error_count == 1);
                REQUIRE(on_completed_count == 0);
            }
        }
        WHEN("Call on_completed")
        {
            observer.on_completed();
            THEN("on_next obtained")
            {
                REQUIRE(on_next_count == 0);
                REQUIRE(on_error_count == 0);
                REQUIRE(on_completed_count == 1);
            }
        }
    };
    GIVEN("specific_observer")
    {
        validate_observer(rpp::specific_observer{[&](const int&               ) { ++on_next_count; },
                                                 [&](const std::exception_ptr&) { ++on_error_count; },
                                                 [&]() { ++on_completed_count; }});
    }
    GIVEN("dynamic_observer")
    {
        validate_observer(rpp::dynamic_observer{[&](const int&               ) { ++on_next_count; },
                                                [&](const std::exception_ptr&) { ++on_error_count; },
                                                [&]() { ++on_completed_count; }});
    }
    GIVEN("dynamic_observer from specific_observer")
    {
        validate_observer(rpp::specific_observer{[&](const int&               ) { ++on_next_count; },
                                                 [&](const std::exception_ptr&) { ++on_error_count; },
                                                 [&]() { ++on_completed_count; }}.as_dynamic());
    }
}

SCENARIO("Any observer can be casted to dynamic_observer")
{
    auto validate_observer =[](const auto& observer)
    {
        WHEN("Call as_dynamic function")
        {
            auto dynamic_observer = observer.as_dynamic();

            THEN("Obtain dynamic_observer of same type")
            {
                static_assert(std::is_same<decltype(dynamic_observer), rpp::dynamic_observer<int>>{}, "Type of dynamic observer should be same!");
            }
        }

        WHEN("Construct dynamic_observer by constructor")
        {
            auto dynamic_observer = rpp::dynamic_observer{observer};

            THEN("Obtain dynamic_observer of same type")
            {
                static_assert(std::is_same<decltype(dynamic_observer), rpp::dynamic_observer<int>>{}, "Type of dynamic observer should be same!");
            }
        }
    };

    GIVEN("specific_observer")
    {
        validate_observer(rpp::specific_observer([](const int&) {}));
    }

    GIVEN("dynamic_observer")
    {
        validate_observer(rpp::dynamic_observer([](const int&) {}));
    }
}