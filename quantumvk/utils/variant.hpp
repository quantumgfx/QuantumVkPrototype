#pragma once

#include <memory>
#include <utility>

namespace Quantum
{
	//Class that can hold one type at any one time.
	class Variant
	{
	public:
		Variant() = default;

		template <typename T>
		explicit Variant(T&& t)
		{
			set(std::forward<T>(t));
		}

		template <typename T>
		void set(T&& t)
		{
			value = std::make_shared<HolderValue<T>>(std::forward<T>(t));
		}

		template <typename T>
		T& get()
		{
			return static_cast<HolderValue<T>*>(value.get())->value;
		}

		template <typename T>
		const T& get() const
		{
			return static_cast<const HolderValue<T>*>(value.get())->value;
		}

	private:
		struct Holder
		{
			virtual ~Holder() = default;
		};

		template <typename U>
		struct HolderValue : Holder
		{
			template <typename P>
			HolderValue(P&& p)
				: value(std::forward<P>(p))
			{
			}

			U value;
		};
		std::shared_ptr<Holder> value;
	};
}