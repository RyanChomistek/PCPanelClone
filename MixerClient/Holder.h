#pragma once

template <class T, class TReleaser>
class Holder
{
public:
	Holder(T p)
		: m_p(p)
	{}

	Holder& operator= (const T& other)
	{
		m_p = other;
		return *this;
	}

	Holder(std::nullptr_t)
		: m_p(nullptr)
	{}

	Holder() = default;

	Holder(Holder&& sp)
		: m_p(sp.m_p)
	{
		sp.m_p = nullptr;
	}

	Holder& operator= (Holder&& other)
	{
		m_p = other.m_p;
		other.m_p = nullptr;
		return *this;
	}

	virtual ~Holder()
	{
		Release();
	}

	void Release()
	{
		TReleaser::Release(m_p);
	}

	T operator->() const
	{
		return m_p;
	}

	T& operator*() const
	{
		return *m_p;
	}

	T* operator&()
	{
		return &m_p;
	}

	bool operator== (Holder other)
	{
		return m_p == other.m_p;
	}

	bool operator!= (Holder other)
	{
		return m_p != other.m_p;
	}

	bool FEmpty() { return m_p == nullptr; }

	T m_p = nullptr;
};