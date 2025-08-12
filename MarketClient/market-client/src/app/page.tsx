"use client";

import { useEffect, useRef, useState } from "react";

export default function Home() {
  const [clientId, setClientId] = useState("");
  const [assetId, setAssetId] = useState("");
  const [amount, setAmount] = useState("");
  const [price, setPrice] = useState("");
  const [orderType, setOrderType] = useState("MARKET");
  const [orderSide, setOrderSide] = useState("BUY");

  const ws = useRef<WebSocket | null>(null);

  useEffect(() => {
    // Check if the WebSocket instance has already been created
    if (!ws.current) {
      ws.current = new WebSocket("ws://localhost:8080");
    }

    // Set up event listeners inside useEffect
    ws.current.onopen = () => {
      console.log("Connected to WebSocket server");
    };

    ws.current.onmessage = (event) => {
      alert(event.data);
    };

    ws.current.onclose = () => {
      console.log("Disconnected from WebSocket server");
    };

    // Clean up function: this runs when the component unmounts
    return () => {
      if (ws.current) {
        ws.current.close();
      }
    };
  }, []);

  function sendOrder() {
    ws.current!.send(
      `ADD_ORDER ${orderSide} ${orderType} ${
        orderType === "LIMIT" ? price : "1"
      } ${amount} ${assetId} ${clientId}`
    );
  }

  return (
    <div className="m-4">
      <h1>Simple orders making page</h1>
      <br />
      <h2>Client ID: {clientId}</h2>
      <input
        type="text"
        value={clientId}
        onChange={(e) => {
          setClientId(e.target.value);
        }}
        className="border"
      />
      <br />
      <br />
      <h2>Asset ID: {assetId}</h2>
      <input
        type="text"
        value={assetId}
        onChange={(e) => {
          setAssetId(e.target.value);
        }}
        className="border"
      />
      <br />
      <br />
      <h2>Order type: {orderType}</h2>
      <select
        name="Order type"
        id="Order type"
        className="border"
        value={orderType}
        onChange={(e) => {
          setOrderType(e.target.value);
        }}
      >
        <option value="MARKET">Market</option>
        <option value="LIMIT">Limit</option>
      </select>
      <br />
      <br />
      <h2>Order side: {orderSide}</h2>
      <select
        name="Order side"
        id="Order side"
        className="border"
        value={orderSide}
        onChange={(e) => {
          setOrderSide(e.target.value);
        }}
      >
        <option value="BUY">Buy</option>
        <option value="SELL">Sell</option>
      </select>
      <br />
      <br />
      <h2>Amount: {amount}</h2>
      <input
        type="text"
        value={amount}
        onChange={(e) => {
          setAmount(e.target.value);
        }}
        className="border"
      />
      <br />
      <br />
      {orderType === "LIMIT" && (
        <div>
          {" "}
          <h2>Price: {price}</h2>
          <input
            type="text"
            value={price}
            onChange={(e) => {
              setPrice(e.target.value);
            }}
            className="border"
          />
          <br />
          <br />
        </div>
      )}
      <button onClick={sendOrder} className={"border"}>
        Send order
      </button>
    </div>
  );
}
