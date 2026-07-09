package com.ssnwt.helloxr;

import android.os.Binder;
import android.os.IBinder;
import android.os.IInterface;
import android.os.Parcel;
import android.os.RemoteException;

public interface IBleCallback extends IInterface {
    String DESCRIPTOR = "com.ssnwt.helloxr.IBleCallback";

    void onCommand(String command) throws RemoteException;

    void onWifiConnected(String ipAddress) throws RemoteException;

    void onWifiFailed(String message) throws RemoteException;

    void onBleConnectionChanged(boolean connected) throws RemoteException;

    class Default implements IBleCallback {
        @Override
        public IBinder asBinder() {
            return null;
        }

        @Override
        public void onCommand(String command) throws RemoteException {
        }

        @Override
        public void onWifiConnected(String ipAddress) throws RemoteException {
        }

        @Override
        public void onWifiFailed(String message) throws RemoteException {
        }

        @Override
        public void onBleConnectionChanged(boolean connected) throws RemoteException {
        }
    }

    abstract class Stub extends Binder implements IBleCallback {
        static final int TRANSACTION_onCommand = 1;
        static final int TRANSACTION_onWifiConnected = 2;
        static final int TRANSACTION_onWifiFailed = 3;
        static final int TRANSACTION_onBleConnectionChanged = 4;

        public Stub() {
            attachInterface(this, DESCRIPTOR);
        }

        public static IBleCallback asInterface(IBinder binder) {
            if (binder == null) {
                return null;
            }
            IInterface local = binder.queryLocalInterface(DESCRIPTOR);
            if (local instanceof IBleCallback) {
                return (IBleCallback) local;
            }
            return new Proxy(binder);
        }

        @Override
        public IBinder asBinder() {
            return this;
        }

        @Override
        public boolean onTransact(int code, Parcel data, Parcel reply, int flags)
                throws RemoteException {
            if (code >= 1 && code <= 16777215) {
                data.enforceInterface(DESCRIPTOR);
            }
            if (code == INTERFACE_TRANSACTION) {
                if (reply != null) {
                    reply.writeString(DESCRIPTOR);
                }
                return true;
            }
            if (code == TRANSACTION_onCommand) {
                onCommand(data.readString());
                return true;
            }
            if (code == TRANSACTION_onWifiConnected) {
                onWifiConnected(data.readString());
                return true;
            }
            if (code == TRANSACTION_onWifiFailed) {
                onWifiFailed(data.readString());
                return true;
            }
            if (code == TRANSACTION_onBleConnectionChanged) {
                onBleConnectionChanged(data.readInt() != 0);
                return true;
            }
            return super.onTransact(code, data, reply, flags);
        }

        public static class Proxy implements IBleCallback {
            private final IBinder remote;

            public Proxy(IBinder remote) {
                this.remote = remote;
            }

            @Override
            public IBinder asBinder() {
                return remote;
            }

            public String getInterfaceDescriptor() {
                return DESCRIPTOR;
            }

            @Override
            public void onCommand(String command) throws RemoteException {
                Parcel data = Parcel.obtain();
                try {
                    data.writeInterfaceToken(DESCRIPTOR);
                    data.writeString(command);
                    remote.transact(TRANSACTION_onCommand, data, null, FLAG_ONEWAY);
                } finally {
                    data.recycle();
                }
            }

            @Override
            public void onWifiConnected(String ipAddress) throws RemoteException {
                Parcel data = Parcel.obtain();
                try {
                    data.writeInterfaceToken(DESCRIPTOR);
                    data.writeString(ipAddress);
                    remote.transact(TRANSACTION_onWifiConnected, data, null, FLAG_ONEWAY);
                } finally {
                    data.recycle();
                }
            }

            @Override
            public void onWifiFailed(String message) throws RemoteException {
                Parcel data = Parcel.obtain();
                try {
                    data.writeInterfaceToken(DESCRIPTOR);
                    data.writeString(message);
                    remote.transact(TRANSACTION_onWifiFailed, data, null, FLAG_ONEWAY);
                } finally {
                    data.recycle();
                }
            }

            @Override
            public void onBleConnectionChanged(boolean connected) throws RemoteException {
                Parcel data = Parcel.obtain();
                try {
                    data.writeInterfaceToken(DESCRIPTOR);
                    data.writeInt(connected ? 1 : 0);
                    remote.transact(
                            TRANSACTION_onBleConnectionChanged, data, null, FLAG_ONEWAY);
                } finally {
                    data.recycle();
                }
            }
        }
    }
}
