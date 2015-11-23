#include <algorithm>
#include <fincu/time/schedule.hpp>
#include <fincu/time/imm.hpp>

namespace FinCu {

    namespace {

        Date nextTwentieth(const Date& d, DateGeneration::Rule rule) {
            Date result = Date(20, d.month(), d.year());
            if (result < d)
                result += 1*Months;
            if (rule == DateGeneration::TwentiethIMM ||
                rule == DateGeneration::OldCDS ||
                rule == DateGeneration::CDS) {
                Month m = result.month();
                if (m % 3 != 0) { // not a main IMM nmonth
                    Integer skip = 3 - m%3;
                    result += skip*Months;
                }
            }
            return result;
        }

        Date previousTwentieth(const Date& d, DateGeneration::Rule rule) {
            Date result = Date(20, d.month(), d.year());
            if (result > d)
                result -= 1*Months;
            if (rule == DateGeneration::TwentiethIMM ||
                rule == DateGeneration::OldCDS ||
                rule == DateGeneration::CDS) {
                Month m = result.month();
                if (m % 3 != 0) { // not a main IMM nmonth
                    Integer skip = m%3;
                    result -= skip*Months;
                }
            }
            return result;
        }
    }


    Schedule::Schedule(const std::vector<Date>& dates,
                       const Calendar& calendar,
                       BusinessDayConvention convention,
                       const std::vector<bool>& isRegular)
    : calendar_(calendar),  convention_(convention),
      dates_(dates), isRegular_(isRegular) {
        FC_REQUIRE(
            isRegular_.size() == 0 || isRegular_.size() == dates.size() - 1,
            "isRegular size ("
                << isRegular_.size()
                << ") must be zero or equal to the number of dates minus 1 ("
                << dates.size() - 1 << ")");
    }

    Schedule::Schedule(Date effectiveDate,
                       const Date& terminationDate,
                       const Period& tenor,
                       const Calendar& cal,
                       BusinessDayConvention convention,
                       BusinessDayConvention terminationDateConvention,
                       DateGeneration::Rule rule,
                       bool endOfMonth,
                       const Date& first,
                       const Date& nextToLast)
    : tenor_(tenor), calendar_(cal), convention_(convention),
      terminationDateConvention_(terminationDateConvention), rule_(rule),
      endOfMonth_(tenor<1*Months ? false : endOfMonth),
      firstDate_(first==effectiveDate ? Date() : first),
      nextToLastDate_(nextToLast==terminationDate ? Date() : nextToLast)
    {
        // sanity checks
		FC_REQUIRE(effectiveDate != Date(), "null effective date");
        FC_REQUIRE(terminationDate != Date(), "null termination date");

        FC_REQUIRE(effectiveDate < terminationDate,
                   "effective date (" << effectiveDate
                   << ") later than or equal to termination date ("
                   << terminationDate << ")");

        if (tenor.length()==0)
            rule_ = DateGeneration::Zero;
        else
            FC_REQUIRE(tenor.length()>0,
                       "non positive tenor (" << tenor << ") not allowed");

        if (firstDate_ != Date()) {
            switch (rule_) {
              case DateGeneration::Backward:
              case DateGeneration::Forward:
                FC_REQUIRE(firstDate_ > effectiveDate &&
                           firstDate_ < terminationDate,
                           "first date (" << firstDate_ <<
                           ") out of effective-termination date range [" <<
                           effectiveDate << ", " << terminationDate << ")");
                // we should ensure that the above condition is still
                // verified after adjustment
                break;
              case DateGeneration::ThirdWednesday:
                  FC_REQUIRE(IMM::isIMMdate(firstDate_, false),
                             "first date (" << firstDate_ <<
                             ") is not an IMM date");
                break;
              case DateGeneration::Zero:
              case DateGeneration::Twentieth:
              case DateGeneration::TwentiethIMM:
              case DateGeneration::OldCDS:
              case DateGeneration::CDS:
                FC_FAIL("first date incompatible with " << rule_ <<
                        " date generation rule");
              default:
                FC_FAIL("unknown rule (" << Integer(rule_) << ")");
            }
        }
        if (nextToLastDate_ != Date()) {
            switch (rule_) {
              case DateGeneration::Backward:
              case DateGeneration::Forward:
                FC_REQUIRE(nextToLastDate_ > effectiveDate &&
                           nextToLastDate_ < terminationDate,
                           "next to last date (" << nextToLastDate_ <<
                           ") out of effective-termination date range (" <<
                           effectiveDate << ", " << terminationDate << "]");
                // we should ensure that the above condition is still
                // verified after adjustment
                break;
              case DateGeneration::ThirdWednesday:
                FC_REQUIRE(IMM::isIMMdate(nextToLastDate_, false),
                           "next-to-last date (" << nextToLastDate_ <<
                           ") is not an IMM date");
                break;
              case DateGeneration::Zero:
              case DateGeneration::Twentieth:
              case DateGeneration::TwentiethIMM:
              case DateGeneration::OldCDS:
              case DateGeneration::CDS:
                FC_FAIL("next to last date incompatible with " << rule_ <<
                        " date generation rule");
              default:
                FC_FAIL("unknown rule (" << Integer(rule_) << ")");
            }
        }


        // calendar needed for endOfMonth adjustment
        Calendar nullCalendar = NullCalendar();
        Integer periods = 1;
        Date seed, exitDate;
        switch (rule_) {

          case DateGeneration::Zero:
            tenor_ = 0*Years;
            dates_.push_back(effectiveDate);
            dates_.push_back(terminationDate);
            isRegular_.push_back(true);
            break;

          case DateGeneration::Backward:

            dates_.push_back(terminationDate);

            seed = terminationDate;
            if (nextToLastDate_ != Date()) {
                dates_.insert(dates_.begin(), nextToLastDate_);
                Date temp = nullCalendar.advance(seed,
                    -periods*(tenor_), convention, endOfMonth_);
                if (temp!=nextToLastDate_)
                    isRegular_.insert(isRegular_.begin(), false);
                else
                    isRegular_.insert(isRegular_.begin(), true);
                seed = nextToLastDate_;
            }

            exitDate = effectiveDate;
            if (firstDate_ != Date())
                exitDate = firstDate_;

            for (;;) {
                Date temp = nullCalendar.advance(seed,
                    -periods*(tenor_), convention, endOfMonth_);
                if (temp < exitDate) {
                    if (firstDate_ != Date() &&
                        (calendar_.adjust(dates_.front(),convention)!=
                         calendar_.adjust(firstDate_,convention))) {
                        dates_.insert(dates_.begin(), firstDate_);
                        isRegular_.insert(isRegular_.begin(), false);
                    }
                    break;
                } else {
                    // skip dates that would result in duplicates
                    // after adjustment
                    if (calendar_.adjust(dates_.front(),convention)!=
                        calendar_.adjust(temp,convention)) {
                        dates_.insert(dates_.begin(), temp);
                        isRegular_.insert(isRegular_.begin(), true);
                    }
                    ++periods;
                }
            }

            if (calendar_.adjust(dates_.front(),convention)!=
                calendar_.adjust(effectiveDate,convention)) {
                dates_.insert(dates_.begin(), effectiveDate);
                isRegular_.insert(isRegular_.begin(), false);
            }
            break;

          case DateGeneration::Twentieth:
          case DateGeneration::TwentiethIMM:
          case DateGeneration::ThirdWednesday:
          case DateGeneration::OldCDS:
          case DateGeneration::CDS:
            FC_REQUIRE(!endOfMonth_,
                       "endOfMonth convention incompatible with " << rule_ <<
                       " date generation rule");
          // fall through
          case DateGeneration::Forward:

            if (rule_ == DateGeneration::CDS) {
                dates_.push_back(previousTwentieth(effectiveDate,
                                                   DateGeneration::CDS));
            } else {
                dates_.push_back(effectiveDate);
            }

            seed = dates_.back();

            if (firstDate_!=Date()) {
                dates_.push_back(firstDate_);
                Date temp = nullCalendar.advance(seed, periods*(tenor_),
                                                 convention, endOfMonth_);
                if (temp!=firstDate_)
                    isRegular_.push_back(false);
                else
                    isRegular_.push_back(true);
                seed = firstDate_;
            } else if (rule_ == DateGeneration::Twentieth ||
                       rule_ == DateGeneration::TwentiethIMM ||
                       rule_ == DateGeneration::OldCDS ||
                       rule_ == DateGeneration::CDS) {
                Date next20th = nextTwentieth(effectiveDate, rule_);
                if (rule_ == DateGeneration::OldCDS) {
                    // distance rule inforced in natural days
                    static const BigInteger stubDays = 30;
                    if (next20th - effectiveDate < stubDays) {
                        // +1 will skip this one and get the next
                        next20th = nextTwentieth(next20th + 1, rule_);
                    }
                }
                if (next20th != effectiveDate) {
                    dates_.push_back(next20th);
                    isRegular_.push_back(false);
                    seed = next20th;
                }
            }

            exitDate = terminationDate;
            if (nextToLastDate_ != Date())
                exitDate = nextToLastDate_;

            for (;;) {
                Date temp = nullCalendar.advance(seed, periods*(tenor_),
                                                 convention, endOfMonth_);
                if (temp > exitDate) {
                    if (nextToLastDate_ != Date() &&
                        (calendar_.adjust(dates_.back(),convention)!=
                         calendar_.adjust(nextToLastDate_,convention))) {
                        dates_.push_back(nextToLastDate_);
                        isRegular_.push_back(false);
                    }
                    break;
                } else {
                    // skip dates that would result in duplicates
                    // after adjustment
                    if (calendar_.adjust(dates_.back(),convention)!=
                        calendar_.adjust(temp,convention)) {
                        dates_.push_back(temp);
                        isRegular_.push_back(true);
                    }
                    ++periods;
                }
            }

            if (calendar_.adjust(dates_.back(),terminationDateConvention)!=
                calendar_.adjust(terminationDate,terminationDateConvention)) {
                if (rule_ == DateGeneration::Twentieth ||
                    rule_ == DateGeneration::TwentiethIMM ||
                    rule_ == DateGeneration::OldCDS ||
                    rule_ == DateGeneration::CDS) {
                    dates_.push_back(nextTwentieth(terminationDate, rule_));
                    isRegular_.push_back(true);
                } else {
                    dates_.push_back(terminationDate);
                    isRegular_.push_back(false);
                }
            }

            break;

          default:
            FC_FAIL("unknown rule (" << Integer(rule_) << ")");
        }

        // adjustments
        if (rule_==DateGeneration::ThirdWednesday)
            for (Size i=1; i<dates_.size()-1; ++i)
                dates_[i] = Date::nthWeekday(3, Wednesday,
                                             dates_[i].month(),
                                             dates_[i].year());

        if (endOfMonth_ && calendar_.isEndOfMonth(seed)) {
            // adjust to end of month
            if (convention == Unadjusted) {
                for (Size i=1; i<dates_.size()-1; ++i)
                    dates_[i] = Date::endOfMonth(dates_[i]);
            } else {
                for (Size i=1; i<dates_.size()-1; ++i)
                    dates_[i] = calendar_.endOfMonth(dates_[i]);
            }
            if (terminationDateConvention != Unadjusted) {
                dates_.front() = calendar_.endOfMonth(dates_.front());
                dates_.back() = calendar_.endOfMonth(dates_.back());
            } else {
                // the termination date is the first if going backwards,
                // the last otherwise.
                if (rule_ == DateGeneration::Backward)
                    dates_.back() = Date::endOfMonth(dates_.back());
                else
                    dates_.front() = Date::endOfMonth(dates_.front());
            }
        } else {
            // first date not adjusted for CDS schedules
            if (rule_ != DateGeneration::OldCDS)
                dates_[0] = calendar_.adjust(dates_[0], convention);
            for (Size i=1; i<dates_.size()-1; ++i)
                dates_[i] = calendar_.adjust(dates_[i], convention);

            // termination date is NOT adjusted as per ISDA
            // specifications, unless otherwise specified in the
            // confirmation of the deal or unless we're creating a CDS
            // schedule
            if (terminationDateConvention != Unadjusted
                || rule_ == DateGeneration::Twentieth
                || rule_ == DateGeneration::TwentiethIMM
                || rule_ == DateGeneration::OldCDS
                || rule_ == DateGeneration::CDS) {
                dates_.back() = calendar_.adjust(dates_.back(),
                                                terminationDateConvention);
            }
        }

        // Final safety checks to remove extra next-to-last date, if
        // necessary.  It can happen to be equal or later than the end
        // date due to EOM adjustments (see the Schedule test suite
        // for an example).
        if (dates_.size() >= 2 && dates_[dates_.size()-2] >= dates_.back()) {
            isRegular_[dates_.size()-2] =
                (dates_[dates_.size()-2] == dates_.back());
            dates_[dates_.size()-2] = dates_.back();
            dates_.pop_back();
            isRegular_.pop_back();
        }
        if (dates_.size() >= 2 && dates_[1] <= dates_.front()) {
            isRegular_[1] =
                (dates_[1] == dates_.front());
            dates_[1] = dates_.front();
            dates_.erase(dates_.begin());
            isRegular_.erase(isRegular_.begin());
        }

        FC_REQUIRE(dates_.size()>1,
            "degenerate single date (" << dates_[0] << ") schedule" <<
            "\n seed date: " << seed <<
            "\n exit date: " << exitDate <<
            "\n effective date: " << effectiveDate <<
            "\n first date: " << first <<
            "\n next to last date: " << nextToLast <<
            "\n termination date: " << terminationDate <<
            "\n generation rule: " << rule_ <<
            "\n end of month: " << endOfMonth_);

    }


    Schedule Schedule::until(const Date& truncationDate) const {
        Schedule result = *this;

        FC_REQUIRE(truncationDate>result.dates_[0],
                   "truncation date " << truncationDate <<
                   " must be later than schedule first date " <<
                   result.dates_[0]);
        if (truncationDate<result.dates_.back()) {
            // remove later dates
            while (result.dates_.back()>truncationDate) {
                result.dates_.pop_back();
                result.isRegular_.pop_back();
            }

            // add truncationDate if missing
            if (truncationDate!=result.dates_.back()) {
                result.dates_.push_back(truncationDate);
                result.isRegular_.push_back(false);
                result.terminationDateConvention_ = Unadjusted;
            } else {
                result.terminationDateConvention_ = convention_;
            }

            if (result.nextToLastDate_>=truncationDate)
                result.nextToLastDate_ = Date();
            if (result.firstDate_>=truncationDate)
                result.firstDate_ = Date();
        }

        return result;
    }

    std::vector<Date>::const_iterator
    Schedule::lower_bound(const Date& refDate) const {
		FC_REQUIRE(refDate != Date(), refDate << "null date given");
        Date d = refDate;
        return std::lower_bound(dates_.begin(), dates_.end(), d);
    }

    Date Schedule::nextDate(const Date& refDate) const {
        std::vector<Date>::const_iterator res = lower_bound(refDate);
        if (res!=dates_.end())
            return *res;
        else
            return Date();
    }

    Date Schedule::previousDate(const Date& refDate) const {
        std::vector<Date>::const_iterator res = lower_bound(refDate);
        if (res!=dates_.begin())
            return *(--res);
        else
            return Date();
    }

    bool Schedule::isRegular(Size i) const {
        FC_REQUIRE(isRegular_.size() > 0,
                   "full interface (isRegular) not available");
        FC_REQUIRE(i<=isRegular_.size() && i>0,
                   "index (" << i << ") must be in [1, " <<
                   isRegular_.size() <<"]");
        return isRegular_[i-1];
    }

    const std::vector<bool>& Schedule::isRegular() const {
        FC_REQUIRE(isRegular_.size() > 0,
                   "full interface (isRegular) not available");
        return isRegular_;
    }

    MakeSchedule::MakeSchedule()
    : rule_(DateGeneration::Backward), endOfMonth_(false) {}

    MakeSchedule& MakeSchedule::from(const Date& effectiveDate) {
        effectiveDate_ = effectiveDate;
        return *this;
    }

    MakeSchedule& MakeSchedule::to(const Date& terminationDate) {
        terminationDate_ = terminationDate;
        return *this;
    }

    MakeSchedule& MakeSchedule::withTenor(const Period& tenor) {
        tenor_ = tenor;
        return *this;
    }

    MakeSchedule& MakeSchedule::withFrequency(Frequency frequency) {
        tenor_ = Period(frequency);
        return *this;
    }

    MakeSchedule& MakeSchedule::withCalendar(const Calendar& calendar) {
        calendar_ = calendar;
        return *this;
    }

    MakeSchedule& MakeSchedule::withConvention(BusinessDayConvention conv) {
        convention_ = conv;
        return *this;
    }

    MakeSchedule& MakeSchedule::withTerminationDateConvention(
                                                BusinessDayConvention conv) {
        terminationDateConvention_ = conv;
        return *this;
    }

    MakeSchedule& MakeSchedule::withRule(DateGeneration::Rule r) {
        rule_ = r;
        return *this;
    }

    MakeSchedule& MakeSchedule::forwards() {
        rule_ = DateGeneration::Forward;
        return *this;
    }

    MakeSchedule& MakeSchedule::backwards() {
        rule_ = DateGeneration::Backward;
        return *this;
    }

    MakeSchedule& MakeSchedule::endOfMonth(bool flag) {
        endOfMonth_ = flag;
        return *this;
    }

    MakeSchedule& MakeSchedule::withFirstDate(const Date& d) {
        firstDate_ = d;
        return *this;
    }

    MakeSchedule& MakeSchedule::withNextToLastDate(const Date& d) {
        nextToLastDate_ = d;
        return *this;
    }

    MakeSchedule::operator Schedule() const {
        // check for mandatory arguments
        FC_REQUIRE(effectiveDate_ != Date(), "effective date not provided");
        FC_REQUIRE(terminationDate_ != Date(), "termination date not provided");

        // set dynamic defaults:
        BusinessDayConvention convention = convention_;

        BusinessDayConvention terminationDateConvention 
								= terminationDateConvention_;

        Calendar calendar = calendar_;
        // if no calendar was set...
        if (calendar.empty()) {
            // ...we use a null one.
            calendar = NullCalendar();
        }

        return Schedule(effectiveDate_, terminationDate_, tenor_, calendar,
                        convention, terminationDateConvention,
                        rule_, endOfMonth_, firstDate_, nextToLastDate_);
    }

}