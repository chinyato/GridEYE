/*
AMG8833sendToUDP.c

Copyright (c) 2020 Yasushi Shirato
Released under the MIT license
http://opensource.org/licenses/mit-license.php
*/

// コンパイル
// gcc -o AMG8833sendToUDP AMG8833sendToUDP.c -lpigpio -lpthread
// 実行
// sudo ./AMG8833sendToUDP
// オプション1  -ip IPアドレス  
// オプション2  -port ポート番号
// 例）  sudo ./AMG8833sendToUDP -ip 192.168.1.255 -port 6060
//
// 参考資料　『参考仕様書　PANA-S-A0002141979-1.pdf』
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <linux/i2c-dev.h>
#include <pigpio.h>
#include <arpa/inet.h>
#include <time.h>
#include <string.h>

//AMG8833のI2Cポート番号定義
#define AMG8833_I2C_ADDRESS 0x68

// UDP初期設定値
#define INIT_IPADDRESS "192.168.1.255"
#define INIT_PORTNO 6501

// 画素データのバッファー ８ｘ８画素　１２８バイト
char buf[128];
//文字列バッファー 1Kバイト
char sendText[ 1024 ];
// 送信先ポート番号
int portNo;
//IPアドレス
char ipAddress[20];

int main( int argc, char *argv[] ) {

    // フレームカウンター
    int ct;
    // GPIO関係
    int status;
    int handle;
    // 記録日時
    struct tm tm;
    struct timeval tv;
    // カメラ本体の温度計測
    char ch, cl;
    int temp;
    // UDP
    struct sockaddr_in addr;
    int sock_descriptor;
    ssize_t send_status;
    // その他
    int i, n;
    int x, y, readpt, th, tl;
    char txt[100];

    // UDPの初期設定
    strcpy( ipAddress, INIT_IPADDRESS );
    portNo = INIT_PORTNO;

    //画面クリア
    printf( "\x1b[2J" );
    //カーソルを行頭に移動
    printf( "\x1b[1;1H");

    // 引数の取得と設定
    if( argc >= 3 ) {
        for( i = 1; i < argc; i++ ) {
            sprintf( txt, "%s", argv[i] );
            if( strcmp( txt, "-ip" ) == 0  && i < argc - 1 ) {
                // IPアドレスの変更
                strncpy( txt, argv[ i + 1 ], 24 );
                txt[ 24 ] = 0x00;
                if( strlen( txt ) >= 7 ) {
                    strcpy( ipAddress, txt );
                    printf( " 送信先IP = %s\n", ipAddress );
                }
            }
            if( strcmp( txt, "-port" ) == 0  && i < argc - 1 ) {
                // UDPポート番号の変更
                strncpy( txt, argv[ i + 1 ], 6 );
                txt[ 6 ] = 0x00;
                if( strlen( txt ) >= 4 ) {
                    n = atoi( txt );
                    if( n >= 1024 && n <= 65535 ){
                        portNo = n;
                        printf( "送信先ポート番号 = %d\n", portNo );
                    }
                }
            }
        }
    }

    // フレームカウンター初期化
    ct = 0;

    // GPIOの初期化
    if ( gpioInitialise( ) < 0 ) { 
        return( -1 );
    }

    // 通信設定
    // UDP ソケット作成
    sock_descriptor = socket( AF_INET, SOCK_DGRAM, 0 );
    if( sock_descriptor < 0 ) {
        perror( "ソケットを開けませんでした。");
        gpioTerminate();
        return( -2 );
    }
    // ポート番号
    addr.sin_port = htons( portNo );
    //IPv4
    addr.sin_family = AF_INET; 
    //送信先のIPアドレス
    addr.sin_addr.s_addr = inet_addr( ipAddress );
    //ブロードキャスト
    int flag = 1;
    setsockopt( sock_descriptor, SOL_SOCKET, SO_BROADCAST, (char *)&flag, sizeof( flag ) );


    // GPIOを開く
    handle = i2cOpen( 1, AMG8833_I2C_ADDRESS, 0 );

    // AMG8833の動作設定
    // パワーコントロールレジスタ　動作モードをノーマルに設定
    i2cWriteByteData( handle, 0x00, 0x00 );
    // 50ms以上待機 (参考仕様書 47-21 の説明による)
    usleep( 50000 );
    // イニシャルリセット
    i2cWriteByteData( handle, 0x01, 0x3F );
    // ２フレーム分(200ms)待機 (参考仕様書 47-21 の説明による)
    usleep( 20000 );
    // フラグリセット
    i2cWriteByteData( handle, 0x01, 0x30 );
    // フレームレート設定
    i2cWriteByteData( handle, 0x02, 0x00 );
    // 割り込みモードに設定
    i2cWriteByteData( handle, 0x03, 0x03 );
    // アベレージモード設定
    i2cWriteByteData( handle, 0x1F, 0x50 );
    i2cWriteByteData( handle, 0x1F, 0x45 );
    i2cWriteByteData( handle, 0x1F, 0x57 );
    // ２回移動平均モードの設定　0x20:有効 0x00:無効
    i2cWriteByteData( handle, 0x07, 0x20 );
    i2cWriteByteData( handle, 0x1F, 0x00 );

    // 以下ループでAMG8833のステータスレジスタの変化を監視して
    // 温度レジスタを読みだす
    while ( 1 ) {
        
        // ステータスレジスタを読む
        status = i2cReadByteData( handle, 0x04 );
        if( status == 0 ) {
            //  変化が無い場合は 10ms待機
            usleep( 10000 );
            continue;
        }
        
        // 温度レジスタから測定値を読みだす
        // 1LSB  0.025℃ 12bit(11bit+サイン)分解能　2の補数形式
        i2cReadI2CBlockData( handle, 0x80, &buf[0],  32 );
        i2cReadI2CBlockData( handle, 0xA0, &buf[32], 32 );
        i2cReadI2CBlockData( handle, 0xC0, &buf[64], 32 );
        i2cReadI2CBlockData( handle, 0xE0, &buf[96], 32 );

        //フラグレジスタをリセット
        i2cWriteByteData( handle, 0x05, 0x06 );

        // サーミスタレジスタ 1LSB  0.0625℃ 12bit分解能　符号+絶対値
        ch = i2cReadByteData( handle, 0x0F );
        cl = i2cReadByteData( handle, 0x0E );
        temp = cl + ( ( ch & 0x07 ) << 8 );

        // 記録日時の取得
        time_t t = time( NULL );
        localtime_r( &t, &tm );
        gettimeofday( &tv, NULL );
        
        // 送信データのヘッダーを作成する
        sprintf( sendText, "Device:AMG8833; Width:8; Height:8; Frame:%d; Thermistor: %5.2f; Date:%04d/%02d/%02d %02d:%02d:%02d,%03d; ThermalData:\r\n",
                            ct, temp * 0.0625,
                            tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                            tm.tm_hour, tm.tm_min, tm.tm_sec,
                            (uint16_t)( tv.tv_usec / 1000 ) );

        // 温度データを作成
        // Windows環境のC++BuilderなどのTBitmapとは画素の並び方が違う
        // AMG8833の背面から撮影方向を見た場合
        // X軸方向　右から左へ
        // Y軸方向　下から上へ
        // 詳細は 参考仕様書　47-5 (P.6) を参照
        
        for ( y = 0; y < 8; y++ ) {
            for ( x = 0; x < 8; x++ ) {
                readpt = ( y << 4 ) + ( x  << 1 );
                th = buf[ readpt + 1 ] ;
                tl = buf[ readpt ];
                // 測定データを文字列へ変換
                sprintf( sendText, "%s%02x,%02x,", sendText, th, tl );
            }
        }
        // 改行コード CRLF追加
        sprintf( sendText, "%s\r\n", sendText );
        
        // UDPでブロードキャストする
        send_status = sendto( sock_descriptor, sendText, strlen( sendText ) + 1, 0, 
                        ( struct sockaddr * )&addr, sizeof( addr ) );
        if( send_status < 0 ) {
            perror( "送信出来ませんでした。" );
            break;
        }
        // フレームカウンター加算
        ct++;
    }
    // UDPソケットを閉じる
    close( sock_descriptor );

    // i2cを閉じる
    i2cClose( handle );

    // GPIOを終了する
    gpioTerminate();

    return ( 0 );
}
