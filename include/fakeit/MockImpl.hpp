/*
 * Copyright (c) 2014 Eran Pe'er.
 *
 * This program is made available under the terms of the MIT License.
 *
 * Created on Mar 10, 2014
 */

#ifndef MockImpl_h__
#define MockImpl_h__

#include <type_traits>
#include <unordered_set>

#include <memory>

#include "mockutils/DynamicProxy.hpp"
#include "fakeit/StubbingImpl.hpp"
#include "fakeit/MethodMockingContext.hpp"
#include "fakeit/DomainObjects.hpp"
#include "fakeit/FakeitContext.hpp"

namespace fakeit {

template<typename C, typename ... baseclasses>
class MockImpl: private MockObject<C>, public virtual ActualInvocationsSource {
public:

	MockImpl(FakeitContext& fakeit, C &obj)
			: MockImpl<C, baseclasses...>(fakeit, obj, true) {
	}

	MockImpl(FakeitContext& fakeit)
			: MockImpl<C, baseclasses...>(fakeit, *(createFakeInstance()), false) {
		FakeObject<C, baseclasses...>* fake = reinterpret_cast<FakeObject<C, baseclasses...>*>(_instance);
		fake->getVirtualTable().setCookie(1, this);
	}

	virtual ~MockImpl() {
		_proxy.detach();
		if (_isOwner) {
			FakeObject<C, baseclasses...>* fake = reinterpret_cast<FakeObject<C, baseclasses...>*>(_instance);
			delete fake;
		}
	}

	void detach() {
		_isOwner = false;
		_proxy.detach();
	}

	/**
	 * Return all actual invocations of this mock.
	 */
	void getActualInvocations(std::unordered_set<Invocation*>& into) const override {
		std::vector<ActualInvocationsSource*> vec;
		_proxy.getMethodMocks(vec);
		for (ActualInvocationsSource * s : vec) {
			s->getActualInvocations(into);
		}
	}

	void reset() {
		_proxy.Reset();
		if (_isOwner) {
			FakeObject<C, baseclasses...>* fake = reinterpret_cast<FakeObject<C, baseclasses...>*>(_instance);
			fake->initializeDataMembersArea();
		}
	}

	virtual C& get() override
	{
		return _proxy.get();
	}

	virtual FakeitContext & getFakeIt() override {
		return _fakeit;
	}

	template<class DATA_TYPE, typename T, typename ... arglist, class = typename std::enable_if<std::is_base_of<T,C>::value>::type>
	DataMemberStubbingRoot<C, DATA_TYPE> stubDataMember(DATA_TYPE T::*member, const arglist&... ctorargs) {
		_proxy.stubDataMember(member, ctorargs...);
		return DataMemberStubbingRoot<T, DATA_TYPE>();
	}

	template<typename R,typename T, typename ... arglist, class = typename std::enable_if<std::is_base_of<T,C>::value>::type>
	MockingContext<R, arglist...> stubMethod(R (T::*vMethod)(arglist...)) {
		return MockingContext<R, arglist...>(new MethodMockingContextImpl <R, arglist...>(*this, vMethod));
	}
	
	DtorMockingContext stubDtor() {
		return DtorMockingContext(new DtorMockingContextImpl(*this));
    }

private:
	DynamicProxy<C, baseclasses...> _proxy;
	C* _instance; //
	bool _isOwner;
	FakeitContext& _fakeit;

    template<typename R, typename ... arglist>
    class MethodMockingContextBase : public MethodMockingContext<R, arglist...>::Context {
    protected:
        MockImpl<C, baseclasses...>& _mock;
        virtual RecordedMethodBody<C, R, arglist...>& getRecordedMethodBody() = 0;

    public:
        MethodMockingContextBase(MockImpl<C, baseclasses...>& mock) : _mock(mock) {}
        virtual ~MethodMockingContextBase() = default;

        void addMethodInvocationHandler(typename ActualInvocation<arglist...>::Matcher* matcher,
                MethodInvocationHandler<R, arglist...>* invocationHandler) {
            getRecordedMethodBody().addMethodInvocationHandler(matcher, invocationHandler);
        }

        void scanActualInvocations(const std::function<void(ActualInvocation<arglist...>&)>& scanner) {
            getRecordedMethodBody().scanActualInvocations(scanner);
        }

        void setMethodDetails(std::string mockName, std::string methodName) {
            getRecordedMethodBody().setMethodDetails(mockName, methodName);
        }

        bool isOfMethod(MethodInfo & method) {
            return getRecordedMethodBody().isOfMethod(method);
        }

        ActualInvocationsSource& getInvolvedMock() {
            return _mock;
        }

        std::string getMethodName() {
            return getRecordedMethodBody().getMethod().name();
        }

    };

    template<typename R, typename ... arglist>
	class MethodMockingContextImpl : public MethodMockingContextBase<R,arglist...> {
		R (C::*_vMethod)(arglist...);

    protected:

		virtual RecordedMethodBody<C, R, arglist...>& getRecordedMethodBody() override {
			return MethodMockingContextBase<R,arglist...>::_mock.stubMethodIfNotStubbed(MethodMockingContextBase<R,arglist...>::_mock._proxy, _vMethod);
		}

	public:
		virtual ~MethodMockingContextImpl() = default;

		MethodMockingContextImpl(MockImpl<C, baseclasses...>& mock, R (C::*vMethod)(arglist...))
				: MethodMockingContextBase<R,arglist...>(mock), _vMethod(vMethod) {
		}

        virtual std::function<R(arglist&...)> getOriginalMethod() override {
			void * mPtr = MethodMockingContextBase<R,arglist...>::_mock.getOriginalMethod(_vMethod);
			C& instance = MethodMockingContextBase<R,arglist...>::_mock.get();
			return [=, &instance](arglist&... args)->R {
				auto m = union_cast<decltype(_vMethod)>(mPtr);
				return ((&instance)->*m)(args...);
			};
		}

	};

	class DtorMockingContextImpl : public MethodMockingContextBase<unsigned int, int> {

    protected:

		virtual RecordedMethodBody<C, unsigned int, int>& getRecordedMethodBody() override {
			return MethodMockingContextBase<unsigned int, int>::_mock.stubDtorIfNotStubbed(MethodMockingContextBase<unsigned int, int>::_mock._proxy);
        }

    public:
        virtual ~DtorMockingContextImpl() = default;

        DtorMockingContextImpl(MockImpl<C, baseclasses...>& mock)
			: MethodMockingContextBase<unsigned int, int>(mock){
        }

		virtual std::function<unsigned int(int&)> getOriginalMethod() override {
			C& instance = MethodMockingContextBase<unsigned int, int>::_mock.get();
			return [=, &instance](int&)->unsigned int {
				return 0;
            };
        }

    };

    static MockImpl<C, baseclasses...>* getMockImpl(void * instance) {
		FakeObject<C, baseclasses...>* fake = reinterpret_cast<FakeObject<C, baseclasses...>*>(instance);
		MockImpl<C, baseclasses...> * mock = reinterpret_cast<MockImpl<C, baseclasses...>*>(fake->getVirtualTable().getCookie(1));
		return mock;
	}

	void unmocked() {
		ActualInvocation<> invocation(nextInvocationOrdinal(), UnknownMethod::instance());
		UnexpectedMethodCallEvent event(UnexpectedType::Unmocked, invocation);
		auto& fakeit = getMockImpl(this)->_fakeit;
		fakeit.handle(event);

		std::string format = fakeit.format(event);
		UnexpectedMethodCallException e(format);
		throw e;
	}

	static C* createFakeInstance() {
		FakeObject<C, baseclasses...>* fake = new FakeObject<C, baseclasses...>();
		//fake->initializeDataMembersArea();
		void* unmockedMethodStubPtr = union_cast<void*>(&MockImpl<C, baseclasses...>::unmocked);
		fake->getVirtualTable().initAll(unmockedMethodStubPtr);
		return reinterpret_cast<C*>(fake);
	}

	template<typename R, typename ... arglist>
	void * getOriginalMethod(R (C::*vMethod)(arglist...)) {
		auto vt = _proxy.getOriginalVT();
		auto offset = VTUtils::getOffset(vMethod);
		void * origMethodPtr = vt.getMethod(offset);
		return origMethodPtr;
	}

	void * getOriginalDtor() {
		auto vt = _proxy.getOriginalVT();
		auto offset = VTUtils::getDestructorOffset<C>();
		void * origMethodPtr = vt.getMethod(offset);
		return origMethodPtr;
	}

	template<typename R, typename ... arglist>
	RecordedMethodBody<C, R, arglist...>& stubMethodIfNotStubbed(DynamicProxy<C, baseclasses...> &proxy, R (C::*vMethod)(arglist...)) {
		if (!proxy.isMethodStubbed(vMethod)) {
			proxy.stubMethod(vMethod, createRecordedMethodBody<R,arglist...>(*this, vMethod));
		}
		Destructable * d = proxy.getMethodMock(vMethod);
		RecordedMethodBody<C, R, arglist...> * methodMock = dynamic_cast<RecordedMethodBody<C, R, arglist...> *>(d);
		return *methodMock;
	}

	RecordedMethodBody<C,unsigned int,int>& stubDtorIfNotStubbed(DynamicProxy<C, baseclasses...> &proxy) {
		if (!proxy.isDtorStubbed()) {
			proxy.stubDtor(createRecordedDtorBody(*this));
		}
		Destructable * d = proxy.getDtorMock();
		RecordedMethodBody<C, unsigned int, int> * dtorMock = dynamic_cast<RecordedMethodBody<C, unsigned int, int> *>(d);
		return *dtorMock;
	}

	MockImpl(FakeitContext& fakeit, C &obj, bool isSpy)
			: _proxy { obj }, _instance(&obj), _isOwner(!isSpy), _fakeit(fakeit) {
	}

	template<typename R, typename ... arglist>
	static RecordedMethodBody<C, R, arglist...> * createRecordedMethodBody(MockObject<C>& mock, R(C::*vMethod)(arglist...)){
		return new RecordedMethodBody<C, R, arglist...>(mock, typeid(vMethod).name());
	}

	static RecordedMethodBody<C, unsigned int, int> * createRecordedDtorBody(MockObject<C>& mock){
		return new RecordedMethodBody<C, unsigned int, int>(mock, "dtor");
	}

};
}

#endif
