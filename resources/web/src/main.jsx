import React from "react";
import {createRoot} from "react-dom/client";
import "reactflow/dist/style.css";
import App from "./App.jsx";
import "./styles.css";

const root = document.getElementById("root");

if (root)
    createRoot(root).render(<App />);
