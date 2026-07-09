package com.ssnwt.helloxr;

import android.os.Binder;
import android.os.IBinder;
import android.os.IInterface;
import android.os.Parcel;
import android.os.RemoteException;

public interface IBleService extends IInterface {
    String DESCRIPTOR = "com.ssnwt.helloxr.IBleService";

    void registerCallback(IBleCallback callback) throws RemoteException;

    void unregisterCallback(IBleCallback callback) throws RemoteException;

    void sendCommandResponse(String response) throws RemoteException;

    void sendErrorMessage(String message) throws RemoteException;

    String getWifiIpAddress() throws RemoteException;

    String getWifiSsid() throws RemoteException;

    class Default implements IBleService {
        @Override
        public IBinder asBinder() {
            return null;
        }

        @Override
        public void registerCallback(IBleCallback callback) throws RemoteException {
        }

        @Override
        public void unregisterCallback(IBleCallback callback) throws RemoteException {
        }

        @Override
        public void sendCommandResponse(String response) throws RemoteException {
        }

        @Override
        public void sendErrorMessage(String message) throws RemoteException {
        }

        @Override
        public String getWifiIpAddress() throws RemoteException {
            return null;
        }

        @Override
        public String getWifiSsid() throws RemoteException {
            return null;
        }
    }

    abstract class Stub extends Binder implements IBleService {
        static final int TRANSACTION_registerCallback = 1;
        static final int TRANSACTION_unregisterCallback = 2;
        static final int TRANSACTION_sendCommandResponse = 3;
        static final int TRANSACTION_sendErrorMessage = 4;
        static final int TRANSACTION_getWifiIpAddress = 5;
        static final int TRANSACTION_getWifiSsid = 6;

        public Stub() {
            attachInterface(this, DESCRIPTOR);
        }

        public static IBleService asInterface(IBinder binder) {
            if (binder == null) {
                return null;
            }
            IInterface local = binder.queryLocalInterface(DESCRIPTOR);
            if (local instanceof IBleService) {
                return (IBleService) local;
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
            switch (code) {
                case TRANSACTION_registerCallback:
                    registerCallback(IBleCallback.Stub.asInterface(data.readStrongBinder()));
                    if (reply != null) {
                        reply.writeNoException();
                    }
                    return true;
                case TRANSACTION_unregisterCallback:
                    unregisterCallback(IBleCallback.Stub.asInterface(data.readStrongBinder()));
                    if (reply != null) {
                        reply.writeNoException();
                    }
                    return true;
                case TRANSACTION_sendCommandResponse:
                    sendCommandResponse(data.readString());
                    if (reply != null) {
                        reply.writeNoException();
                    }
                    return true;
                case TRANSACTION_sendErrorMessage:
                    sendErrorMessage(data.readString());
                    if (reply != null) {
                        reply.writeNoException();
                    }
                    return true;
                case TRANSACTION_getWifiIpAddress:
                    String wifiIpAddress = getWifiIpAddress();
                    if (reply != null) {
                        reply.writeNoException();
                        reply.writeString(wifiIpAddress);
                    }
                    return true;
                case TRANSACTION_getWifiSsid:
                    String wifiSsid = getWifiSsid();
                    if (reply != null) {
                        reply.writeNoException();
                        reply.writeString(wifiSsid);
                    }
                    return true;
                default:
                    return super.onTransact(code, data, reply, flags);
            }
        }

        public static class Proxy implements IBleService {
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
            public void registerCallback(IBleCallback callback) throws RemoteException {
                Parcel data = Parcel.obtain();
                Parcel reply = Parcel.obtain();
                try {
                    data.writeInterfaceToken(DESCRIPTOR);
                    data.writeStrongInterface(callback);
                    remote.transact(TRANSACTION_registerCallback, data, reply, 0);
                    reply.readException();
                } finally {
                    reply.recycle();
                    data.recycle();
                }
            }

            @Override
            public void unregisterCallback(IBleCallback callback) throws RemoteException {
                Parcel data = Parcel.obtain();
                Parcel reply = Parcel.obtain();
                try {
                    data.writeInterfaceToken(DESCRIPTOR);
                    data.writeStrongInterface(callback);
                    remote.transact(TRANSACTION_unregisterCallback, data, reply, 0);
                    reply.readException();
                } finally {
                    reply.recycle();
                    data.recycle();
                }
            }

            @Override
            public void sendCommandResponse(String response) throws RemoteException {
                Parcel data = Parcel.obtain();
                Parcel reply = Parcel.obtain();
                try {
                    data.writeInterfaceToken(DESCRIPTOR);
                    data.writeString(response);
                    remote.transact(TRANSACTION_sendCommandResponse, data, reply, 0);
                    reply.readException();
                } finally {
                    reply.recycle();
                    data.recycle();
                }
            }

            @Override
            public void sendErrorMessage(String message) throws RemoteException {
                Parcel data = Parcel.obtain();
                Parcel reply = Parcel.obtain();
                try {
                    data.writeInterfaceToken(DESCRIPTOR);
                    data.writeString(message);
                    remote.transact(TRANSACTION_sendErrorMessage, data, reply, 0);
                    reply.readException();
                } finally {
                    reply.recycle();
                    data.recycle();
                }
            }

            @Override
            public String getWifiIpAddress() throws RemoteException {
                Parcel data = Parcel.obtain();
                Parcel reply = Parcel.obtain();
                try {
                    data.writeInterfaceToken(DESCRIPTOR);
                    remote.transact(TRANSACTION_getWifiIpAddress, data, reply, 0);
                    reply.readException();
                    return reply.readString();
                } finally {
                    reply.recycle();
                    data.recycle();
                }
            }

            @Override
            public String getWifiSsid() throws RemoteException {
                Parcel data = Parcel.obtain();
                Parcel reply = Parcel.obtain();
                try {
                    data.writeInterfaceToken(DESCRIPTOR);
                    remote.transact(TRANSACTION_getWifiSsid, data, reply, 0);
                    reply.readException();
                    return reply.readString();
                } finally {
                    reply.recycle();
                    data.recycle();
                }
            }
        }
    }
}
