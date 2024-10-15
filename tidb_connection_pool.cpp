#include <cppconn/connection.h>
#include <cppconn/resultset.h>
#include<iostream>
#include <memory>
#include<mysql_driver.h>
#include<mysql_connection.h>
#include<cppconn/statement.h>
#include<cppconn/prepared_statement.h>
#include<vector>
#include<mutex>
#include<condition_variable>
#include<memory.h>

class ConnectionPool{
public:
    ConnectionPool(const std::string& url,const std::string& user,const std::string&password,int poolSize)
        : url_(url), user_(user), password_(password),poolSize(poolSize)    
    {   
        /*
        connections_.push_back(std::move(conn));：将创建的连接添加到连接池 connections_ 中。这里使用 std::move() 是为了避免在 push_back 时额外拷贝 conn，而是将其所有权转移到 connections_ 容器中，这样可以提高效率。
        connections_ 是一个容器（如 std::vector<std::shared_ptr<sql::Connection>>），用于存储一组共享指针，每个指针指向一个数据库连接
        */
        for(int i=0;i<poolSize;++i){
            auto conn=createConnection();
            connections_.push_back(std::move(conn));
        }
    }

    ~ConnectionPool(){
        for(auto& conn : connections_){
            conn->close();
        }
    }
    //这段代码实现了一个从数据库连接池中获取连接的函数 
    std::shared_ptr<sql::Connection> getConnection(){
        std::unique_lock<std::mutex>lock(mutex_);
        // 让当前线程进入等待状态，直到连接池中有可用的连接
        condition_.wait(lock,[this]{return !connections_.empty();});
        //获取连接
        auto conn=connections_.back();
        connections_.pop_back();
        return conn;
    }

    void releaseConnection(std::shared_ptr<sql::Connection> conn){
        std::lock_guard<std::mutex>lock(mutex_);
        connections_.push_back(std::move(conn));
        condition_.notify_one();
    }

private:
    //创建一个新的数据库的连接
    std::shared_ptr<sql::Connection>createConnection(){
        //这行代码获取了一个 MySQL 驱动实例（MySQL_Driver），它是连接 MySQL 或兼容数据库（比如 TiDB）的基础类
        sql::mysql::MySQL_Driver* driver=sql::mysql::get_mysql_driver_instance(); 
        //建立连接
        return std::shared_ptr<sql::Connection>(driver->connect(url_,user_,password_));
    }

    std::string url_;
    std::string user_;
    std::string password_;
    int poolSize;
    std::vector<std::shared_ptr<sql::Connection>> connections_;
    std::mutex mutex_;
    std::condition_variable condition_;
};

int main(){
    const std::string url="tcp://127.0.0.1:4000"; //Tidb地址
    const std::string user="root";
    const std::string password="";

    ConnectionPool pool(url,user,password,10);  //// 创建连接池，最多10个连接

    //获取连接
    auto conn=pool.getConnection();

    //sql::Statement对象用于执行静态 SQL 查询，即不带参数的 SQL 语句
    //创建statement
    std::unique_ptr<sql::Statement> stmt(conn->createStatement());
    stmt->execute("CREATE DATABASE IF NOT EXISTS testdb");
    stmt->execute("USE testdb");
    stmt->execute("CREATE TABLE IF NOT EXISTS users (id INT PRIMARY KEY AUTO_INCREMENT, name VARCHAR(50), age INT)");
    
    /*
    sql::PreparedStatement 对象用于执行带参数的 SQL 查询，特别是动态的 SQL 插入、更新或删除语句。与 Statement 不同，它能够防止 SQL 注入攻击。
    prepareStatement() 用于创建预编译的 SQL 语句，并且支持占位符（如 ?），占位符将在后续绑定动态参数时替换为具体的值
    */
    //插入数据
    std::unique_ptr<sql::PreparedStatement> pstmt(conn->prepareStatement("INSERT INTO users (name, age) VALUES (?, ?)"));
    pstmt->setString(1,"Alice"); //设置第一个参数为string类型的alice
    pstmt->setInt(2, 25);  //设置第二个参数
    pstmt->execute();  //执行

    pstmt->setString(1, "Bob");
    pstmt->setInt(2, 30);
    pstmt->execute();

    //查询数据
    std::unique_ptr<sql::ResultSet> res(stmt->executeQuery("SELECT * FROM users"));
    while (res->next()) {
        std::cout << "ID: " << res->getInt("id") << ", Name: " << res->getString("name") << ", Age: " << res->getInt("age") << std::endl;
    }

    //释放连接池
    pool.releaseConnection(conn);

    return 0;
}