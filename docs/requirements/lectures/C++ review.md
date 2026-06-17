### 가상함수와 동적 바인딩.
- 연관 강의 : Window Abstraction
- virtual
    - 쓰는 이유 = 상속받았을 때 상속받은 객체 이름과 타입으로 쓸 수 있게 하려고.
    - 쓰는 방식 : 클래스 내에 보이지 않는 가상함수 테이블을 만들어서 연결.
        - 만약에 Window를 상속받는 BigWindow가 있다면 virtual 함수에 대해서 instance.BigWindow::IsVSync();
- 정의할 때 BigWindow에서 virtual bool IsVSync() const override와 같이 재정의함.
- override
    - 기능 : 컴파일러가 override를 보고 함수가 그냥 이름만 재정의된 건지 parameter 등이 다 바뀐 건지 확인.
- 가상 함수를 사용하는 클래스라면 가상 소멸자도 필요함.
- 가상 함수를 구현하지 않고 =0 으로 표시한다면 순수 가상함수이므로 자식 함수에서 꼭 구현해야 함!!
- Window는 모든 멤버가 가상 함수인 인터페이스이므로, 엔진 쪽에서는 구체적인 플랫폼을 몰라도 됨.
- Windows에서 실행하면 WindowsWindow::IsVSync(), Linux에서 실행하면 LinuxWindow::IsVSync가 호출되는 식.
### C++ 함수를 인자로 받기
- 연관 강의 : Window Abstraction & GLFW Callbacks
1. 함수 포인터
```
#include <iostream>

int AddOne(int value)
{
    return value + 1;
}

void Execute(int (*func)(int)) //int를 받아서 int를 반환하는 함수의 포인터
{
    int result = func(10);
    std::cout << result << "\n";
}

int main()
{
    Execute(AddOne);
}
```
2. std::function
- std::function은 함수뿐만 아니라 람다와 함수 객체도 받을 수 있음
    - 함수 객체(function object, functor) : 함수 호출 연산자를 오버로딩해서 class();와 같이 동작할 수 있게 만듦.
    ```
        class Printer
    {
    public:
        void operator()(int value)
        {
            std::cout << value << "\n";
        }
    };
    ```
    - -> printer(10)은 printer.operator()(10)으로 해석됨.
    - 함수객체를 쓰는 이유? 함수가 상태를 가질 수 있기 때문
    ```
    #include <iostream>

    class Add
    {
    public:
        Add(int amount)
            : m_Amount(amount)
        {
        }

        int operator()(int value)
        {
            return value + m_Amount;
        }

    private:
        int m_Amount;
    };

    int main()
    {
        Add addFive(5);
        Add addTen(10);

        std::cout << addFive(3) << "\n"; // 8
        std::cout << addTen(3) << "\n";  // 13
    }
    ```

```
#include <iostream>
#include <functional>

void Hello()
{
    std::cout << "Hello!\n";
}

void Execute(std::function<void()> func)
{
    func();
}

int main()
{
    Execute(Hello);
}
```
3. 람다
- 이름 없는 함수 객체를 컴파일 타임에 즉석으로 만들어 주는 문법
```
auto lambda = [capture](parameters) -> return_type
{
    body
}
```
- 캡처 : 람다 밖의 변수를 람다 안에서 사용

| 문법          | 의미                   |
| ----------- | -------------------- |
| `[]`        | 아무것도 캡처하지 않음         |
| `[x]`       | `x`를 값으로 캡처          |
| `[&x]`      | `x`를 참조로 캡처          |
| `[=]`       | 사용한 외부 변수를 전부 값으로 캡처 |
| `[&]`       | 사용한 외부 변수를 전부 참조로 캡처 |
| `[this]`    | 현재 객체 포인터를 캡처        |
| `[this, x]` | 현재 객체와 `x`를 캡처       |
- 이때 값으로 캡처한 변수는 람다 안에서 수정할 수 없음. 수정하고 싶다면 `mutable`을 붙일 것.
- 람다는 std::function에 저장할 수 있음

### 스마트 포인터
1. unique_ptr
- 포인터를 한 곳에서만 소유하게 만들고, 그 소유자가 사라질 때 객체도 자동으로 삭제
- new/delete의 메모리 누수를 막을 수 있음.
- 복사를 막음
- 소유권 이동은 가능
- 일반 포인터처럼 `->`와 `*`를 사용 가능
- `auto window = std::make_unique<Window>()`로 `std::unique_ptr<Window> ptr(new Window());`처럼 직접 new를 사용하는 것보다 안전하게 생성할 수 있음.
```
void CreateWindow()
{
    std::unique_ptr<Window> window = std::make_unique<Window>();

    window->Init();
} // 여기서 window가 사라지면서 Window 객체도 자동 delete

std::unique_ptr<Window> a = std::make_unique<Window>();

std::unique_ptr<Window> b = a; // 오류
```
- unique_ptr를 함수에 넘기고 싶을 때
    - 그냥 사용만 하고 싶다? 참조를 사용
    - 소유권을 넘기고 싶다면 std::move를 사용.
    ```
    void SetWindow(std::unique_ptr<Window> window)
    {
        m_Window = std::move(window);
    }

    auto window = std::make_unique<WindowsWindow>();

    app.SetWindow(std::move(window)); //이제 app이 window를 소유하게 됨.
    ```
2. 스마트 포인터의 종류

| 포인터          | 의미            |
| ------------ | ------------- |
| `unique_ptr` | 소유자가 하나       |
| `shared_ptr` | 여러 곳이 공동 소유   |
| `weak_ptr`   | 소유하지 않고 관찰만 함 |
