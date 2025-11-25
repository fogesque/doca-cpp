# doca-cpp

C++ Adapter For NVIDIA DOCA Framework 

TODO: Переименовать название модуля в нечто неупоминающее DOCA

## Library Overview

NVIDIA DOCA это большой фреймворк для оффлоада сетевых операций. Фреймворк состоит из множества библиотек. Данный репозиторий создан с целью написать для библиотек DOCA Common, DOCA RDMA, DOCA Flow и DOCA Comch, предоставляющих API на языке C, ООП обертки на языке C++ для того, чтобы функционалом библиотек можно было пользоваться из коробки.

На данный момент разрабатываются обертки над DOCA Common и DOCA RDMA. 

DOCA Common оперирует со следующими сущностями:
- Устройство:
   * `Device`
- Память:
   * `Buffer`
   * `BufferInventory`
   * `MemoryMap`
- Runtime:
   * `Context`
   * `ProgressEngine`
   * `Task`

`Device` представляет сетевое устройство, например ConnectX-6 DX SmartNIC. 
`Buffer` является абстракцией над областью памяти и нужен для манипуляций над памятью через API DOCA. `BufferInventory` это некий контейнер для `Buffer`.  `MemoryMap` это отображение памяти для доступа к ней устройства.
Большинство операций в DOCA выполняются асинхронно, выполнение операций приводит к вызову callback-функций, которые задаются пользователем и являются индикатором того, что операция выполнена. Для того, чтобы вызвать триггер на вызов callback, необходимо выполнять polling для `ProgressEngine`. Context необходим для прикрепления `Task` к контексту выполнения и для прикрепления его самого к `ProgressEngine`.

DOCA RDMA оперирует со следующими сущностями:
- Runtime:
   * `Rdma`
   * `RdmaContext`
   * `RdmaTask`
   * `RdmaConnection`

`Rdma` это структура, объединяющая в себе управление всей логикой RDMA операций. `RdmaContext` это контекст RDMA, который присоединяется к `ProgressEngine`. `RdmaTask` это операция RDMA (`RdmaTaskSend`, `RdmaTaskReceive` и другие). `RdmaConnection` служит для управления соединением между пирами в сети.

DOCA RDMA поддерживает множество операций RDMA, но автор держит фокус на следующих:
- Send
- Receive
- Write
- Read

DOCA RDMA позволяет устанавливаеть соединение клиент-сервер или точка-точка. 
В первом случае используется RDMA CM (Connection Manager). Сервер слушает порт ОС и принимает запросы на подключения. Клиент подключается к заранее известному адресу сервера. В этом режиме используется RoCEv2.
Во втором случае используется Out Of Band коммуникация, при которой соединение экспортируется и передается между машинами другим каналом или через флешку. В этом режиме используется RoCEv1.

До или после установки соединения клиент и сервер могут создавать заадачи для RDMA операций. Задачи Receive и Send зеркальны в том смысле, что, при установке задачи Send на одной стороне, на другой должна быть установлена задача Receive, и наоборот. Операции Write и Read не требуют задачи на другом пире.

В терминах DOCA два пира в RDMA CM - это Requester и Responder. Ниже стрелкой показано, в какую сторону и при какой операции отправится сетевой пакет.

| Task          | Data Direction                               |
| :---          | :---                                         |
| Send/Receive  | Side Submitted Send ⟶ Side Submitted Receive |
| Write         | Write Requester ⟶ Write Responder            |
| Read          | Read Responder ⟶ Read Requester              |

Чтобы выполнить операции Write или Read, Requester должен получить у Responder дескриптор памяти, в которую он хочет записать данные из своего буфера или из которой он хочет прочитать данные в свой буфер. Дескриптор передается с помощью Send/Receive операций.

Приведу пример. Чтобы выполнить операцию Write, Requester создает задачу Receive и ожидает получения дескриптора от Responder. Responder создает задачу Send и отправляет дескриптор. Requester, получив дескриптор, создает `MemoryMap` используя дескриптор и создает задачу Write. Сетевой пакет отправляется и сетевое устройство у Responder кладет payload в память.

Вспомнив, что завершение операции определяется callback-функцией, мы видим проблему: как Responder поймет, что в его память были записаны данные и как понять, когда это произошло?
На данный момент ничего, кроме как добавлять дополнительный обмен пакетами, в голову не приходит. В API DOCA решение не найдено.

## Architecture Overview

При построении архитектуры была допущена ошибка, которая объясняется тем, что автор решил писать код параллельно изучению примеров от DOCA. Эта ошибка будет исправляться в ближайшем времени.

Изначально архитектура выглядела следующим образом. Оберткой над DOCA RDMA служит `RdmaEngine`, который содержит в себе `RdmaConnectionManager`. `RdmaConnectionManager` имеет методы `ListenToPort()`, `AcceptConnection()` для сервера и `Connect()` для клиента. Так как весь код DOCA построен на callback-функциях, было принято решение менять состояние переменной в каждом callback, затем, запуская `ProgressEngine` в цикле, ожидать изменения этой переменной. Например:
```C++
// Will be called when connection state will become disconnect
void ConnectionDisconnectionCallback(doca_rdma_connection * rdmaConnection, 
    union doca_data connectionUserData, union doca_data ctxUserData)
{
    // Retrieve RdmaEngine from doca_data
    auto rdmaEngine = static_cast<RdmaEngine *>(ctxUserData.ptr);
    // Change RdmaEngine internal connection state
    rdmaEngine->SetConnectionState(RdmaConnection::State::disconnected);
}
```
Затем, чтобы определить, что состояние соединения изменилось, необходимо опрашивать `ProgressEngine`:
```C++
...
while (this->rdmaConnection->GetState() != RdmaConnection::State::requested) {
    // Progress RDMA engine to handle incoming connection requests
    this->rdmaEngine->Progress();
}
...
```
`RdmaEngine` выполняет подключение и устанавливает задачи в отдельном потоке. Для этого есть класс `RdmaExecutor`, который создает пул потоков в размере одного потока и имеет метод `SubmitOperation(RdmaBuffer buffer, RdmaOperationType type)` для того, чтобы передать в поток буфер для операции и ее тип. Операции кладутся в очередь на выполнение.

Изначально было решено сделать класс `RdmaPeer` с методами `Send()`, `Receive()`, `Write()`, `Read()`, которые просто бы вызывали `RdmaExecutor::SubmitOperation()`, получали бы `RdmaAwaitable`, который имеет под капотом `std::future` для ожидания результата операции. `RdmaServer` и `RdmaClient` наследуются от `RdmaPeer` и не добавляют ничего более. Разница лишь в том, какие методы будут вызваны у `RdmaConnectionManager` для соединения.

В текущем варианте код пользователя для сервера и клиента выглядел бы так:
```C++
void ClientUsageExample() {
    // Create client
    auto client = RdmaClient::Create();

    // Perform Send
    auto sendBuffer = RdmaBuffer::Create();
    auto awaitable = client.Send(sendBuffer);

    // Wait for result
    awaitable.Await() // Will wait for Send callback

    // Perform Write
    auto writeBuffer = RdmaBuffer::Create();
    auto awaitable = client.Write(writeBuffer);

    // Wait for result
    awaitable.Await() // Will wait for Write callback
}

void ServerUsageExample() {
    // Create server
    auto server = RdmaServer::Create();

    // Perform Receive
    auto receiveBuffer = RdmaBuffer::Create();
    auto awaitable = server.Receive(receiveBuffer);

    // Wait for result
    awaitable.Await() // Will wait for Receive callback

    // PROBLEM:
    // How to handle client's Write Task?
}
```

Проблема дизайна уже очевидна исходя из описанного в конце Library Overview. Метод Write() содержит вызовы:

```C++
void RdmaPeer::Write() {
    // Simplified code
    Buffer sourceBuffer;
    Buffer descriptor;
    this->rdmaEngine->Receive(descriptor);
    auto mmap = MemoryMap::CreateFromDescriptor(descriptor);
    auto destBuffer = Buffer::GetFromMmap(mmap);
    this->rdmaEbgine->Write(sourceBuffer, destBuffer);
}
```

То есть, чтобы принять данные, `RdmaServer` должен установить Send. Это несколько не вписывается в текущий дизайн, а также проблема с синхронизацией Write или Read остается.

Такая архитектура, хоть и использует сущности клиент и сервер, не является клиент-серверной. Здесь скорее зеркальное общение между пирами, которое должно быть синхронно по операциям.

Клиент-серверная архитектура подразумевает, что сервер слушает подключения на порте и отвечает на запросы. Клиент же подключается и шлет эти запросы, получая ответ. Какие могут быть запросы в случае RDMA? Ответ: такие же, как операции, но с указанием направления, которое будет у данных. В итоге список возможных запросов:
* Send To Server
* Receive From Server
* Write To Server
* Read From Server
* Send To Requester
* Receive From Requester
* Write To Requester
* Read From Requester

С одной стороны, кажется, что 4 из 8 операции дублирующие по смыслу. С другой стороны, стоит отметить, что каждая из них определяет, кто Requester, а кто Responder. Однако условившись, что Requester = Client и Responder = Server, мы зачеркиваем лишние операции.

В итоге правильный интерфейс выглядел бы так: 
```C++
void ClientUsageExample() {
    // Create client
    auto client = RdmaClient::Create();

    // Perform Send
    auto sendBuffer = RdmaBuffer::Create();
    auto awaitable = client.Send(sendBuffer);

    // Wait for result
    awaitable.Await() // Will wait for Send callback

    // Perform Write
    auto writeBuffer = RdmaBuffer::Create();
    auto awaitable = client.Write(writeBuffer);

    // Wait for result
    awaitable.Await() // Will wait for Write callback
}

void ServerUsageExample() {
    // Create server
    auto server = RdmaServer::Create();

    // Listen and get requests
    server.Serve();

    // PROBLEM:
    // Which Task to submit inside of Serve()?
}
```

Однако такая архитектура поднимает вопрос: а как сервер должен понимать, что от него запрашивает клиент? Какую задачу надо ставить?

Эти вопросы приводят к необходимости создать свой протокол. Например, сервер будет ставить задачу Receive для каждого подключения, и клиент должен сделать Send с указанием вида операции, которую он хочет провести. Затем сервер и клиент зеркально ставят задачи под выбранную операцию.

Но как быть с Write? Если клиент поставит Write, как сервер поймет, что в его буфер записаны данные?

Эта проблема может быть решена добавлением Receive задачи для сервера и Send задачи для клиента после выполнения задачи Write. 

## RDMA Problems

На данный момент автор столкнулся со следующими проблемами.

* Какая схема взаимодействия подходит для RDMA: зеркальная точка-точка или клиент-сервер?
* Как организовать синхронизацию операций Read и Write?
* Как спроектировать сервер для обработки запросов клиента? Какой протокол составить?
* Как сделать схему точка-точка с контрактом, в котором будут указаны желаемые операции?
* Что нужно делать с буферами на сервере, с которыми работает клиент? Сколько их должно быть и какого размера? Как организовать интерфейс для доступа к ним?

Также автор уже задумывается над следующими вещами:

* Как добиться максимальной производительности RDMA в DOCA? Какие размеры буферов, какое количество?
* Можно ли переиспользовать буферы, мапы памяти и дескрипторы для операций в цикле?

## Notes

Автор видит возможную схожесть между RDMA и gRPC. В gRPC есть унарная, серверная потоковая передача, клиентская потоковая передача и двунаправленная потоковая передача. Для RDMA можно было бы создавать похожий контракт и генерировать код с вызовом нужных операций под капотом прозрачных для пользователя вызовов по типу `client.service.SendSomething(something);`

По контракту мы сможем определить весь набор операций, который нужен для выполнения вызова сервиса из него и при желании сгенерировать под каждый из них код.